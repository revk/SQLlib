// General SQL client application
// Designed to execute distinct queries
// Expands environment variables with quoting for sql
// Various output formats
// (c) Adrian Kennard 2007

#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <popt.h>
#include <ctype.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <signal.h>
#include <execinfo.h>
#include "sqllib.h"

const char *sqlconf = NULL;
const char *sqlhost = NULL;
unsigned int sqlport = 0;
const char *sqluser = NULL;
const char *sqlpass = NULL;
const char *sqldatabase = NULL;
const char *csvsol = "";
const char *csveol = "";
const char *csvnull = "";
const char *csvcomma = ",";
int jsarray = 0;
int slow = 0;
int trans = 0;
int debug = 0;
int headers = 0;
int noexpand = 0;
int reportid = 0;
int reportchanges = 0;
int statuschanges = 0;
int ret = 0;
int linesplit = 0;
int safe = 0;
int unsafe = 0;
int csvout = 0;
int abortdeadlock = 0;

SQL sql;
SQL_RES *res;
SQL_ROW row;

void
dosql (const char *origquery)
{
   char *query = strdupa (origquery);
   char *qalloc = NULL;
   int par = 0,
      hash = 0,
      comma = 0;
   if (!noexpand)
   {                            // expand query
      int nalloc = 0,
         p = 0;
      char *i;
      char quote = 0;
      i = query + strlen (query);
      while (i > query && i[-1] < ' ')
         i--;
      if (i > query && i[-1] == ';')
         i--;
      *i = 0;
#define add(c)	{if(p>=nalloc&&!(qalloc=realloc(qalloc,nalloc+=1000)))err(5,"malloc %d",nalloc);qalloc[p++]=(c);}
      void addquoted (char c, int tab)
      {
         if ((tab && c == '\t') || c == ',')
         {
            if (hash || comma)
               add (quote && quote != '"' ? quote : '\'');
            add (',');
            if (hash || comma)
               add (quote && quote != '"' ? quote : '\'');
         } else if (quote == '`')
         {
            if ((unsigned char) c >= ' ')
               add (c);
         } else if (c == '\'')
         {
            add ('\'');
            add ('\'');
         } else if (c == '\\')
         {
            add ('\\');
            add ('\\');
         } else if (c == '\n')
         {
            add ('\\');
            add ('n');
         } else if (c == '\r')
         {
            add ('\\');
            add ('r');
         } else if (c == '\t')
         {
            add ('\\');
            add ('t');
         } else if ((unsigned char) c >= ' ' || c == '\f')
            add (c);
      }
      for (i = query; *i; i++)
      {
         if (quote && quote == *i)
         {
            add ((quote == '"') ? '\'' : quote);
            quote = 0;
            continue;
         }
         if (!quote && (*i == '\'' || *i == '"' || *i == '`'))
         {
            quote = *i;
            add ((quote == '"') ? '\'' : quote);
            continue;
         }
         if (quote && *i == '\'')
         {
            add ('\'');
            add ('\'');
            continue;
         }
         if (*i == '\\' && i[1])
         {
            add (*i);
            i++;
            add (*i);
            continue;
         }
         if (*i == '$' && i[1] == '-')
         {                      // stdin
            int c;
            if (!quote)
               add ('\'');
            while ((c = getchar ()) >= 0)
               addquoted (c, 0);
            if (!quote)
               add ('\'');
            i++;
            continue;
         }
         if (*i == '$' && i[1] == '$')
         {
            add (*i++);
            continue;
         }
         if (!quote && *i == ';')
            errx (5, "Multiple command");
         if (*i == '$')
         {                      // More general case
            char was;
            char *e,
             *b = NULL,
               esc = 0,
               file = 0;
            comma = hash = 0;
            char *q = i;
            i++;
            // Prefixes
            while (*i)
               if (*i == '#')
               {
                  hash = 1;
                  i++;
                  continue;
               } else if (*i == ',')
               {
                  comma = 1;
                  i++;
                  continue;
               } else if (*i == '@')
               {
                  file = 1;
                  i++;
                  continue;
               } else
                  break;
            if (*i == '{' && isalpha (i[1]))
            {
               esc = 1;
               i++;
               b = i;
               while (*i && *i != '}')
                  i++;
               if (*i != '}')
               {
                  i = q;
                  b = NULL;
               }
            } else if (isalpha (*i))
            {
               b = i;
               while (isalnum (*i) || *i == '_')
                  i++;
            } else
               i = q;
            if (b)
            {                   // We have variable...
               was = *i;
               *i = 0;
               e = getenv (b);
               if (!e)
               {
                  if (debug)
                     fprintf (stderr, "No variable $%s\n", b);
                  *i = was;
                  if (!esc)
                     i--;
                  continue;
               }
               if (file)
               {
                  FILE *f = fopen (e, "r");
                  if (!f)
                  {
                     if (debug)
                        fprintf (stderr, "No file $%s (%s)\n", b, e);
                     *i = was;
                     if (!esc)
                        i--;
                     continue;
                  }
                  int c;
                  if (!quote)
                     add ('\'');
                  while ((c = fgetc (f)) >= 0)
                     addquoted (c, 0);
                  if (!quote)
                     add ('\'');
                  fclose (f);
               } else
               {
                  if ((hash || comma))
                  {             // special quoting
                     if (!quote)
                        add ('\'');
                     while (*e)
                        addquoted (*e++, comma);
                     if (!quote)
                        add ('\'');
                  } else if (quote == '`')
                  {
                     while (*e)
                     {          // limited field name variable expansion
                        if (isalnum (*e) || *e == '.')
                           add (*e);
                        e++;
                     }
                  } else if (quote)
                  {             // other quoted variable name expansion
                     while (*e)
                        addquoted (*e++, 0);
                  } else
                  {             // allow simple numbers if not quoted
                     char *q = e;
                     int l = 0;
                     while (*q)
                     {
                        if ((*q == '+' || *q == '-' || *q == '(') && q[1])
                        {
                           if (*q == '(')
                              l++;
                           q++;
                        }
                        if (!isdigit (*q) && *q != '.')
                           break;
                        while (isdigit (*q))
                           q++;
                        if (*q == '.')
                        {
                           q++;
                           while (isdigit (*q))
                              q++;
                           if (*q == 'e' || *q == 'E')
                           {
                              q++;
                              if (*q == '+' || *q == '-')
                                 q++;
                              while (isdigit (*q))
                                 q++;
                           }
                        }
                        while (*q == ')')
                        {
                           l--;
                           q++;
                        }
                        while (*q == ' ')
                           q++;
                        if ((*q == '*' || *q == '/' || *q == '+' || *q == '-') && q[1])
                           q++; // simple maths
                        while (*q == ' ')
                           q++;
                     }
                     if (*q || l)
                     {
                        add ('0');      // if not valid use 0 as a syntactically valid option
                        if (debug)
                           fprintf (stderr, "Invalid syntax in variable, %s\n", e);
                     } else
                        while (*e)
                           add (*e++);
                  }
               }
               *i = was;
               if (!esc)
                  i--;
               continue;
            }
         }
         if (*i == '(')
            par++;
         else if (*i == ')')
            par--;
         add (*i);
      }
      add (0);
      query = qalloc;
   }
   int err = sql_query (&sql, query);
   if (err && !safe && !unsafe && sql_errno (&sql) == ER_UPDATE_WITHOUT_KEY_IN_SAFE_MODE)
   {
      warnx ("SQL warning:%s\n[%s]\n[%s]", sql_error (&sql), query, origquery);
      sql_safe_query (&sql, "SET SQL_SAFE_UPDATES=0");
      err = sql_query (&sql, query);
   }
   if (err == ER_LOCK_DEADLOCK && !abortdeadlock && !trans)
      err = sql_query (&sql, query);    // Auto retry - just the once
   if (err)
   {
      trans = 0;                // Abort
      ret++;
      if (trans)
         errx (1, "SQL error:%s\n[%s]\n[%s]", sql_error (&sql), query, origquery);
      else
         warnx ("SQL error:%s\n[%s]\n[%s]", sql_error (&sql), query, origquery);
   } else
   {
      if (debug)
         fprintf (stderr, "[%s]\n", query);
      res = sql_store_result (&sql);
      if (res)
      {
         int fields = sql_num_fields (res),
            f;
         SQL_FIELD *field = sql_fetch_field (res);
#if 0
         if (debug)
         {
            fprintf (stderr, "Type\tLen\tName\n");
            for (f = 0; f < fields; f++)
               fprintf (stderr, "%d\t%lu\t%s\t", field[f].type, field[f].length, field[f].name);
         }
#endif
         if (csvout)
         {
            void s (char *s)
            {
               putchar ('"');
               while (*s)
               {
                  if (*s == '\n')
                     printf ("\\n");
                  else if (*s == '\r')
                     printf ("\\r");
                  else if (*s == '\t')
                     printf ("\\t");
                  else
                  {
                     if (*s == '\\' || *s == '\"')
                        putchar ('\\');
                     putchar (*s);
                  }
                  s++;
               }
               putchar ('"');
            }
            int line = 0;
            if (jsarray)
               printf ("%s", csvsol);
            if (headers)
            {
               if (line++)
                  printf ("%s\n", jsarray ? csvcomma : "");
               printf ("%s", csvsol);
               for (f = 0; f < fields; f++)
               {
                  if (f)
                     printf ("%s", csvcomma);
                  s (field[f].name);
               }
               printf ("%s", csveol);
            }
            while ((row = sql_fetch_row (res)))
            {
               if (line++)
                  printf ("%s\n", jsarray ? csvcomma : "");
               printf ("%s", csvsol);
               for (f = 0; f < fields; f++)
               {
                  if (f)
                     printf ("%s", csvcomma);
                  // NULL is nothing, i.e. ,, or configured null value
                  // Strings are quoted even if empty
                  // Numerics are unquoted
                  if (row[f] && !strncasecmp (field[f].name, "json_", 5))
                     printf ("%s", row[f]);     // Special case, sql already JSON coded
                  else if (row[f])
                  {
                     if (IS_NUM (field[f].type) && field[f].type != FIELD_TYPE_TIMESTAMP)
                        printf ("%s", row[f]);
                     else
                        s (row[f]);
                  } else
                     printf ("%s", csvnull);
               }
               printf ("%s", csveol);
            }
            printf ("%s\n", jsarray ? csveol : "");
         } else
         {
            if (headers)
            {
               for (f = 0; f < fields; f++)
               {
                  if (f)
                     putchar ('\t');
                  printf ("%s", field[f].name);
               }
               putchar ('\n');
            }
            while ((row = sql_fetch_row (res)))
            {
               for (f = 0; f < fields; f++)
               {
                  if (f)
                     putchar (linesplit ? '\n' : '\t');
                  if (!row[f])
                     printf ("NULL");
                  else if (*row[f])
                     printf ("%s", row[f]);
                  else if (linesplit)
                     putchar (' ');
               }
               putchar ('\n');
            }
         }
         sql_free_result (res);
      } else
      {
         if (reportid)
         {
            unsigned long long i = sql_insert_id (&sql);
            if (i)
               printf ("%llu\n", sql_insert_id (&sql));
         }
         if (reportchanges)
            printf ("%llu\n", sql_affected_rows (&sql));
         if (statuschanges && !sql_affected_rows (&sql))
            ret++;
      }
   }
   if (qalloc)
      free (qalloc);
   if (slow)
      usleep (10000);
}

int
main (int argc, const char *argv[])
{
   const char *defcsvsol = csvsol;
   const char *defcsveol = csveol;
   const char *defcsvnull = csvnull;
   const char *defcsvcomma = csvcomma;
   poptContext popt;            // context for parsing command-line options
   const struct poptOption optionsTable[] = {
      {"sql-conf", 0, POPT_ARGFLAG_SHOW_DEFAULT | POPT_ARG_STRING, &sqlconf, 0, "Client config file", "filename"},
      {"sql-host", 'h', POPT_ARG_STRING, &sqlhost, 0, "SQL server host", "hostname/ip"},
      {"sql-port", 0, POPT_ARG_INT, &sqlport, 0, "SQL server port", "port"},
      {
       "sql-user", 'u', POPT_ARG_STRING, &sqluser, 0, "SQL username", "username"},
      {
       "sql-pass", 'p', POPT_ARG_STRING, &sqlpass, 0, "SQL password", "password"},
      {
       "sql-database", 'd', POPT_ARG_STRING, &sqldatabase, 0, "SQL database", "database"},
      {"debug", 'v', POPT_ARG_NONE, &debug, 0, "Debug", 0},
      {"sql-debug", 'V', POPT_ARG_NONE, &sqldebug, 0, "SQL Debug", 0},
      {"id", 'i', POPT_ARG_NONE, &reportid, 0, "Print insert ID", 0},
      {"changes", 'c', POPT_ARG_NONE, &reportchanges, 0, "Report how many rows changes", 0},
      {"safe", 0, POPT_ARG_NONE, &safe, 0, "Use safe mode (default is to warn but continue)", 0},
      {"unsafe", 0, POPT_ARG_NONE, &unsafe, 0, "Use unsafe mode (default is to warn but continue)", 0},
      {"status-changes", 'C', POPT_ARG_NONE, &statuschanges, 0, "Return non zero status if no changes were made", 0},
      {"no-expand", 'x', POPT_ARG_NONE, &noexpand, 0, "Don't expand env variables", 0},
      {"transaction", 't', POPT_ARG_NONE, &trans, 0, "Run sequence of commands as a transaction", 0},
      {"abort-deadlock", 'A', POPT_ARG_NONE, &abortdeadlock, 0, "Do not retry single command on deadlock error", 0},
      {"csv", 0, POPT_ARG_NONE, &csvout, 0, "Output in CSV", 0},
      {"csv-sol", 0, POPT_ARG_STRING, &csvsol, 0, "Start of each line for CSV", 0},
      {"csv-eol", 0, POPT_ARG_STRING, &csveol, 0, "End of each line for CSV", 0},
      {"csv-null", 0, POPT_ARG_STRING, &csvnull, 0, "NULL in CSV", 0},
      {"csv-comma", 0, POPT_ARGFLAG_SHOW_DEFAULT | POPT_ARG_STRING, &csvcomma, 0, "Comma in CSV", 0},
      {"jsarray", 0, POPT_ARG_NONE, &jsarray, 0, "Javascript array", 0},
      {
       "line-split", 'l', POPT_ARG_NONE, &linesplit, 0,
       "Put each field on a new line, and a single space if empty string, for use in (\"`...`\") in csh", 0},
      {"headers", 'H', POPT_ARG_NONE, &headers, 0, "Headers", 0},
      {"slow", 0, POPT_ARG_NONE, &slow, 0, "Pause between commands", 0},
      POPT_AUTOHELP {
                     NULL, 0, 0, NULL, 0}
   };

   popt = poptGetContext (NULL, argc, argv, optionsTable, 0);
   poptSetOtherOptionHelp (popt, "[database] '<sql-commands>' (May include $VAR, $- for stdin, $+ for UUID)");
   /* Now do options processing, get portname */
   {
      int c = poptGetNextOpt (popt);
      if (c < -1)
         errx (1, "%s: %s\n", poptBadOption (popt, POPT_BADOPTION_NOALIAS), poptStrerror (c));
   }

   if (safe && unsafe)
      errx (1, "Do the safety dance");

   if (jsarray)
   {                            // Alternative defaults for jsarray
      csvout = 1;
      if (defcsvsol == csvsol)
         csvsol = "[";
      if (defcsveol == csveol)
         csveol = "]";
      if (defcsvnull == csvnull)
         csvnull = "null";
      if (defcsvcomma == csvcomma)
         csvcomma = ",";
   }

   if (!sqldatabase && poptPeekArg (popt))
      sqldatabase = poptGetArg (popt);

   if (sqldatabase && !*sqldatabase)
      sqldatabase = NULL;

   sql_real_connect (&sql, sqlhost, sqluser, sqlpass, sqldatabase, sqlport, 0, 0, 1, sqlconf);

   if (trans)
      dosql ("START TRANSACTION");
   if (!unsafe)
      sql_safe_query (&sql, "SET SQL_SAFE_UPDATES=1");
   if (!poptPeekArg (popt))
   {                            // stdin
      char *line = NULL;
      size_t linespace = 0;
      ssize_t len = 0;
      while ((len = getline (&line, &linespace, stdin)) > 0)
         dosql (line);
      if (line)
         free (line);
   } else
      while (poptPeekArg (popt))
         dosql (poptGetArg (popt));
   if (trans)
      dosql ("COMMIT");
   sql_close (&sql);
   return ret;
}
