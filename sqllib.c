// SQL client library
// Copyright (c) Adrian Kennard 2007
// This software is provided under the terms of the GPL v2 or later.
// This software is provided free of charge with a full "Money back" guarantee.
// Use entirely at your own risk. We accept no liability. If you don't like that - don't use it.

#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include <time.h>
#include <sys/time.h>
#include <err.h>
#include <stdlib.h>
#include <ctype.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include "sqllib.h"

int sqldebug = 0;               // Set 1 to print queries & errors, 2 for just errors, -ve to not do any actual updates just print
int sqlsyslogerror = -1;
int sqlsyslogquery = -1;
const char *sqlcnf = "~/.my.cnf";       // Default
const char *capem = "/etc/mysql/cacert.pem";

SQL *
sql_real_connect (MYSQL * sql, const char *host, const char *user, const char *passwd, const char *db,
                  unsigned int port, const char *unix_socket, unsigned long client_flag, char safe, const char *mycnf)
{                               // Connect but check config file
   const char *sslca = NULL;
   const char *sslcert = NULL;
   const char *sslkey = NULL;
   const char *skipssl = NULL;
   if (!access (capem, R_OK))
      sslca = capem;            // Default CA certificate PEM file, if present we assume we should use SSL/TLS
   if (safe && sql)
      sql_init (sql);
   if (!mycnf)
   {
      mycnf = getenv ("SQL_CNF");
      if (!mycnf)
         mycnf = sqlcnf;
   }
   if (mycnf && *mycnf)
   {
      char *fn = (char *) mycnf;
      if (*fn == '~')
         if (asprintf (&fn, "%s%s", getenv ("MYSQL_HOME") ? : getenv ("HOME") ? : "/etc", fn + 1) < 0)
            errx (1, "malloc at line %d", __LINE__);
      struct stat s;
      if (!stat (fn, &s) && S_ISREG (s.st_mode))
      {
         if ((s.st_mode & 0577) != 0400)
            warnx ("%s is not user only read, not using.", fn);
         else
         {
            FILE *f = fopen (fn, "r");
            if (!f)
               warnx ("Cannot open %s", fn);
            else
            {
               char *l = NULL;
               size_t lspace = 0;
               ssize_t len = 0;
               while ((len = getline (&l, &lspace, f)) > 0)
                  if (!strncasecmp (l, "[client]", 8))
                     break;
               while ((len = getline (&l, &lspace, f)) > 0 && *l != '[')
               {                // read client lines
                  char *e = l + len,
                     *p;
                  while (e > l && e[-1] < ' ')
                     e--;
                  *e = 0;
                  for (p = l; isalnum (*p) || *p == '-'; p++);
                  char *v = NULL;
                  for (v = p; isspace (*v); v++);
                  if (*v++ == '=')
                     for (; isspace (*v); v++);
                  else
                     v = NULL;
                  if (v && *v == '\'' && e > v && e[-1] == '\'')
                  {             // Quoted
                     v++;
                     *--e = 0;
                  }
                  const char **set = NULL;
                  if ((p - l) == 4 && !strncasecmp (l, "port", p - l))
                  {
                     if (!port && v)
                        port = atoi (v);
                  } else if ((p - l) == 4 && !strncasecmp (l, "user", p - l))
                     set = &user;
                  else if ((p - l) == 4 && !strncasecmp (l, "host", p - l))
                     set = &host;
                  else if ((p - l) == 8 && !strncasecmp (l, "password", p - l))
                     set = &passwd;
                  else if ((p - l) == 8 && !strncasecmp (l, "database", p - l))
                     set = &db;
                  else if ((p - l) == 6 && !strncasecmp (l, "ssl-ca", p - l))
                     set = &sslca;
                  else if ((p - l) == 8 && !strncasecmp (l, "ssl-cert", p - l))
                     set = &sslcert;
                  else if ((p - l) == 7 && !strncasecmp (l, "ssl-key", p - l))
                     set = &sslkey;
                  else if ((p - l) == 8 && !strncasecmp (l, "skip-ssl", p - l))
                     set = &skipssl;
                  if (set && !*set && v)
                  {
                     if (!*v)
                        *set = NULL;    // Allow unset
                     else
                        *set = strdupa (v);
                  }
               }
               if (l)
                  free (l);
               fclose (f);
            }
         }
      }
      if (fn != mycnf)
         free (fn);
   }
   my_bool reconnect = 1;
   sql_options (sql, MYSQL_OPT_RECONNECT, &reconnect);
   int allow = 1;
   sql_options (sql, MYSQL_OPT_LOCAL_INFILE, &allow);   // Was previously allowed
   if (host && (!*host || !strcasecmp (host, "localhost")))
      host = NULL;              // A blank host as local connection
   if (host && (sslkey || sslcert || sslca) && !skipssl)
   {                            // SSL (TLS) settings
#if MYSQL_VERSION < 10
      errx (1, "No SSL available in this SQL library build");
#else
      if (sslkey)
         sql_options (sql, MYSQL_OPT_SSL_KEY, sslkey);
      if (sslcert)
         sql_options (sql, MYSQL_OPT_SSL_CERT, sslcert);
      if (sslca)
      {
         sql_options (sql, MYSQL_OPT_SSL_CA, sslca);
         int check = 1;
         sql_options (sql, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, &check);
      }
      client_flag |= CLIENT_SSL;
#endif
   }
   SQL *s = mysql_real_connect (sql, host, user, passwd, db, port, unix_socket, client_flag);
   if (!s)
   {
      if (sqlsyslogerror >= 0)
         syslog (sqlsyslogerror, "%s", sql_error (sql));
      if (safe)
         errx (3, "SQL error accessing '%s': %s", host ? : "(local)", sql_error (sql));
      else if (sqldebug)
         fprintf (stderr, "SQL error accessing '%s': %s\n", host ? : "(local)", sql_error (sql));
   } else
      sql_options (s, MYSQL_SET_CHARSET_NAME, "utf8");  // Seems to be needed after connect?
   return s;
}

int
sql_safe_select_db (SQL * sql, const char *db)
{
   if (sqlsyslogquery >= 0)
      syslog (sqlsyslogquery, "USE %s", db);
   if (sqldebug)
      fprintf (stderr, "USE %s\n", db);
   int e = sql_select_db (sql, db);
   if (e)
   {
      if (sqlsyslogerror >= 0)
         syslog (sqlsyslogerror, "USE %s", sql_error (sql));
      if (!sqldebug)
         fprintf (stderr, "SQL failed: %s\nUSE %s\n", sql_error (sql), db);
      errx (1, "SQL query failed");
   }
   return e;
}

void
sql_safe_query (SQL * sql, char *q)
{
   if (sqldebug < 0)
   {                            // don't do
      if (sqlsyslogquery >= 0)
         syslog (sqlsyslogquery, "%s", q);
      fprintf (stderr, "%s\n", q);
      return;
   }
   int e = sql_query (sql, q);
   if (e == ER_LOCK_DEADLOCK && strcasecmp (q, "COMMIT"))
      e = sql_query (sql, q);   // auto retry once for deadlock in "safe" queries...
   if (e)
   {
      if (!sqldebug)
         fprintf (stderr, "SQL failed (%s): %s\n%s\n", sql->db, sql_error (sql), q);
      errx (1, "SQL query failed");
   }
}

SQL_RES *
sql_safe_query_use (SQL * sql, char *q)
{
   SQL_RES *r;
   sql_safe_query (sql, q);
   r = sql_use_result (sql);
   if (!r)
   {
      if (!sqldebug)
         fprintf (stderr, "%s\n", q);
      errx (1, "SQL query no result");
   }
   return r;
}

SQL_RES *
sql_safe_query_store (SQL * sql, char *q)
{
   SQL_RES *r;
   if (sql_query (sql, q))
   {
      if (!sqldebug)
         fprintf (stderr, "SQL failed (%s): %s\n%s\n", sql->db, sql_error (sql), q);
      errx (1, "SQL query failed");
   }
   r = sql_store_result (sql);
   if (!r)
   {
      if (!sqldebug)
         fprintf (stderr, "%s\n", q);
      errx (1, "SQL query no result");
   } else if (sqldebug)
      fprintf (stderr, "(%llu row%s)\n", sql_num_rows (r), sql_num_rows (r) == 1 ? "" : "s");
   return r;
}

SQL_RES *
sql_query_use (SQL * sql, char *q)
{
   if (sql_query (sql, q))
      return 0;
   return sql_use_result (sql);
}

SQL_RES *
sql_query_store (SQL * sql, char *q)
{
   if (sql_query (sql, q))
      return 0;
   return sql_store_result (sql);
}

int
sql_query (SQL * sql, char *q)
{
   struct timeval a = { }, b = {
   };
   gettimeofday (&a, NULL);
   int r = sql_real_query (sql, q);
   gettimeofday (&b, NULL);
   long long us =
      ((long long) b.tv_sec * 1000000LL + (long long) b.tv_usec) - ((long long) a.tv_sec * 1000000LL + (long long) a.tv_usec);
   if (sqlsyslogquery >= 0)
      syslog (sqlsyslogquery, "%lluus: %s", us, q);
   if (sqlsyslogerror >= 0 && r)
   {
      if (sqlsyslogquery < 0)
         syslog (sqlsyslogerror, "%lluus: %s", us, q);
      syslog (sqlsyslogerror, "%s", sql_error (sql));
   }
   if (sqldebug == 1)
   {
      fprintf (stderr, "%llu.%06llus: %s\n", us / 1000000LL, us % 1000000LL, q);
      if (r)
         fprintf (stderr, "SQL failed (%s): %s\n", sql->db, sql_error (sql));
   }
   return r;
}

void
sql_free_s (sql_string_t * q)
{                               // Free string
   if (q->query)
      free (q->query);
   q->query = NULL;
   q->len = 0;
   q->ptr = 0;
}

char
sql_back_s (sql_string_t * q)
{                               // Remove last character
   if (!q || !q->query || !q->ptr)
      return 0;
   q->ptr--;
   char r = q->query[q->ptr];
   q->query[q->ptr] = 0;
   return r;
}

// Freeing versions for use with malloc'd queries (e.g. from sql_printf...
void
sql_safe_query_s (SQL * sql, sql_string_t * q)
{
   if (!q || !q->query)
      return;
   sql_safe_query (sql, q->query);
   sql_free_s (q);
}

SQL_RES *
sql_safe_query_use_s (SQL * sql, sql_string_t * q)
{
   if (!q || !q->query)
      return 0;
   SQL_RES *r = sql_safe_query_use (sql, q->query);
   sql_free_s (q);
   return r;
}

SQL_RES *
sql_safe_query_store_s (SQL * sql, sql_string_t * q)
{
   if (!q || !q->query)
      return 0;
   SQL_RES *r = sql_safe_query_store (sql, q->query);
   sql_free_s (q);
   return r;
}

SQL_RES *
sql_query_use_s (SQL * sql, sql_string_t * q)
{
   if (!q || !q->query)
      return 0;
   SQL_RES *r = sql_query_use (sql, q->query);
   sql_free_s (q);
   return r;
}

SQL_RES *
sql_query_store_s (SQL * sql, sql_string_t * q)
{
   if (!q || !q->query)
      return 0;
   SQL_RES *r = sql_query_store (sql, q->query);
   sql_free_s (q);
   return r;
}

int
sql_query_s (SQL * sql, sql_string_t * q)
{
   if (!q || !q->query)
      return 0;
   int r = sql_query (sql, q->query);
   sql_free_s (q);
   return r;
}

void
sql_safe_query_free (SQL * sql, char *q)
{
   if (!q)
      return;
   sql_safe_query (sql, q);
   free (q);
}

SQL_RES *
sql_safe_query_use_free (SQL * sql, char *q)
{
   if (!q)
      return 0;
   SQL_RES *r = sql_safe_query_use (sql, q);
   free (q);
   return r;
}

SQL_RES *
sql_safe_query_store_free (SQL * sql, char *q)
{
   if (!q)
      return 0;
   SQL_RES *r = sql_safe_query_store (sql, q);
   free (q);
   return r;
}

SQL_RES *
sql_query_use_free (SQL * sql, char *q)
{
   if (!q)
      return 0;
   SQL_RES *r = sql_query_use (sql, q);
   free (q);
   return r;
}

SQL_RES *
sql_query_store_free (SQL * sql, char *q)
{
   if (!q)
      return 0;
   SQL_RES *r = sql_query_store (sql, q);
   free (q);
   return r;
}

int
sql_query_free (SQL * sql, char *q)
{
   if (!q)
      return 0;
   int r = sql_query (sql, q);
   free (q);
   return r;
}

// SQL formatting print:-
// Formatting is printf style and try to support most printf functions
// Special prefixes
// #    Alternative form, used on 's' and 'c' causes sql quoting, so use %#s for quoted sql strings, %#S for unquoted escaped
// does not support *m$ format width of precision
// Extra format controls
// T    Time, takes time_t argument and formats as sql datetime
// B    Bool, makes 'true' or 'false' based in int argument. With # makes quoted "Y" or "N"

void
sql_vsprintf (sql_string_t * s, const char *f, va_list ap)
{                               // Formatted print, append to query string
   while (*f)
   {
      // check enough space for anothing but a string espansion...
      if (s->ptr + 100 >= s->len && !(s->query = realloc (s->query, s->len += 1000)))
         errx (1, "malloc at line %d", __LINE__);
      if (*f != '%')
      {
         s->query[s->ptr++] = *f++;
         continue;
      }
      // formatting  
      const char *base = f++;
#if 0
      char flagaltint = 0;
      char flagcomma = 0;
      char flagplus = 0;
      char flagspace = 0;
      char flagzero = 0;
#endif
      char flagfree = 0;
      char flagalt = 0;
      char flagleft = 0;
      char flaglong = 0;
      char flaglonglong = 0;
      char flaglongdouble = 0;
      int width = 0;
      int precision = -2;       // indicate not set
      // format modifiers
      while (*f)
      {
#if 0
         if (*f == 'I')
            flagaltint = 1;
         else if (*f == '\'')
            flagcomma = 1;
         else if (*f == '+')
            flagplus = 1;
         else if (*f == ' ')
            flagspace = 1;
         else if (*f == '0')
            flagzero = 1;
         else
#endif
         if (*f == '!')
            flagfree = 1;
         else if (*f == '#')
            flagalt = 1;
         else if (*f == '-')
            flagleft = 1;
         else
            break;
         f++;
      }
      // width
      if (*f == '*')
      {
         width = -1;
         f++;
      } else
         while (isdigit (*f))
            width = width * 10 + (*f++) - '0';
      if (*f == '.')
      {                         // precision
         f++;
         if (*f == '*')
         {
            precision = -1;     // get later
            f++;
         } else
         {
            precision = 0;
            while (isdigit (*f))
               precision = precision * 10 + (*f++) - '0';
         }
      }
      // length modifier
      if (*f == 'h' && f[1] == 'h')
         f += 2;
      else if (*f == 'l' && f[1] == 'l')
      {
         flaglonglong = 1;
         f += 2;
      } else if (*f == 'l')
      {
         flaglong = 1;
         f++;
      } else if (*f == 'L')
      {
         flaglongdouble = 1;
         f++;
      } else if (strchr ("hqjzt", *f))
         f++;

      if (*f == '%')
      {                         // literal!
         s->query[s->ptr++] = *f++;
         continue;
      }

      if (!strchr ("diouxXeEfFgGaAcsCSpnmTUBZ", *f) || f - base > 20)
      {                         // cannot handle, output as is
         while (base < f)
            s->query[s->ptr++] = *base++;
         continue;
      }
      char fmt[22];
      memmove (fmt, base, f - base + 1);
      fmt[f - base + 1] = 0;
      if (strchr ("scTUBSZ", *f))
      {                         // our formatting
         if (width < 0)
            width = va_arg (ap, int);
         if (precision == -1)
            precision = va_arg (ap, int);
         switch (*f)
         {
         case 'S':
         case 's':             // string
            {
               char *a = va_arg (ap, char *);
               if (!a)
               {
                  if (flagalt)
                     s->ptr += sprintf (s->query + s->ptr, "NULL");
                  break;
               }
               int l = 0,       // work out length and quoted length
                  q = 0;
               while ((precision < 0 || l < precision) && a[l])
               {
                  if (a[l] == '\'' || a[l] == '\\' || a[l] == '\n' || a[l] == '\r' || a[l] == '`' || a[l] == '"')
                     q++;
                  l++;          // find width
               }
               if (flagalt)
                  q = l + q;    // quoted length
               else
                  q = l;
               if (width && l < width)
                  q += width - l;
               if (s->ptr + q + 100 >= s->len && !(s->query = realloc (s->query, s->len += q + 1000)))
                  errx (1, "malloc at line %d", __LINE__);
               if (flagalt && *f == 's')
                  s->query[s->ptr++] = '\'';
               if (width && !flagleft && l < width)
               {                // pre padding
                  while (l < width)
                  {
                     s->query[s->ptr++] = ' ';
                     l++;
                  }
               }
               while (*a)
               {
                  if (flagalt && *a == '\n')
                  {
                     s->query[s->ptr++] = '\\';
                     s->query[s->ptr++] = 'n';
                  } else if (flagalt && *a == '\r')
                  {
                     s->query[s->ptr++] = '\\';
                     s->query[s->ptr++] = 'r';
                  } else
                  {
                     if (flagalt && (*a == '\'' || *a == '\\' || *a == '`' || *a == '"'))
                        s->query[s->ptr++] = '\\';
                     s->query[s->ptr++] = *a;
                  }
                  a++;
                  if (precision > 0 && !--precision)
                     break;     // length limited
               }
               if (width && flagleft && l < width)
               {                // post padding
                  while (l < width)
                  {
                     s->query[s->ptr++] = ' ';
                     l++;
                  }
               }
               if (flagalt && *f == 's')
                  s->query[s->ptr++] = '\'';
               if (flagfree && a)
                  free (a);
            }
            break;
         case 'c':             // char
            {
               long long a;
               if (flaglong)
                  a = va_arg (ap, long);
               else if (flaglonglong)
                  a = va_arg (ap, long long);
               else
                  a = va_arg (ap, int);
               if (!a)
               {
                  s->ptr += sprintf (s->query + s->ptr, "NULL");
                  break;
               }
               if (flagalt)
                  s->query[s->ptr++] = '\'';
               if (flagalt && a == '\n')
               {
                  s->query[s->ptr++] = '\\';
                  s->query[s->ptr++] = 'n';
               } else
               {
                  if (flagalt && a == '\'')
                     s->query[s->ptr++] = '\'';
                  s->query[s->ptr++] = a;
               }
               if (flagalt)
                  s->query[s->ptr++] = '\'';
            }
            break;
         case 'Z':             // time (utc)
            {
               time_t a = va_arg (ap, time_t);
               if (flagalt)
                  s->query[s->ptr++] = '\'';
               if (!a)
                  s->ptr += sprintf (s->query + s->ptr, "0000-00-00");
               else
               {
                  struct tm *t = gmtime (&a);
                  int l = sprintf (s->query + s->ptr, "%04u-%02u-%02u %02u:%02u:%02u", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                                   t->tm_hour, t->tm_min, t->tm_sec);
                  if (width > 0 && l > width)
                     l = width;
                  s->ptr += l;
               }
               if (flagalt)
                  s->query[s->ptr++] = '\'';
            }
            break;
         case 'T':             // time
            {
               time_t a = va_arg (ap, time_t);
               if (flagalt)
                  s->query[s->ptr++] = '\'';
               if (!a)
                  s->ptr += sprintf (s->query + s->ptr, "0000-00-00");
               else
               {
                  struct tm *t = localtime (&a);
                  int l = sprintf (s->query + s->ptr, "%04u-%02u-%02u %02u:%02u:%02u", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                                   t->tm_hour, t->tm_min, t->tm_sec);
                  if (width > 0 && l > width)
                     l = width;
                  s->ptr += l;
               }
               if (flagalt)
                  s->query[s->ptr++] = '\'';
            }
            break;
         case 'U':             // time (UTC)
            {
               time_t a = va_arg (ap, time_t);
               if (flagalt)
                  s->query[s->ptr++] = '\'';
               if (!a)
                  s->ptr += sprintf (s->query + s->ptr, "0000-00-00");
               else
               {
                  struct tm *t = gmtime (&a);
                  int l = sprintf (s->query + s->ptr, "%04u-%02u-%02u %02u:%02u:%02u", t->tm_year + 1900, t->tm_mon + 1, t->tm_mday,
                                   t->tm_hour, t->tm_min, t->tm_sec);
                  if (width > 0 && l > width)
                     l = width;
                  s->ptr += l;
               }
               if (flagalt)
                  s->query[s->ptr++] = '\'';
            }
            break;
         case 'B':             // bool
            {
               long long a;
               if (flaglong)
                  a = va_arg (ap, long);
               else if (flaglonglong)
                  a = va_arg (ap, long long);
               else
                  a = va_arg (ap, int);
               if (flagalt)
                  s->ptr += sprintf (s->query + s->ptr, a ? "'Y'" : "'N'");
               else
                  s->ptr += sprintf (s->query + s->ptr, a ? "TRUE" : "FALSE");
            }
            break;
         }
      } else                    // use standard format (non portable code, assumes ap is moved on)
      {
#ifdef NONPORTABLE
         s->ptr += vsnprintf (s->query + s->ptr, s->len - s->ptr, fmt, ap);
#else
         va_list xp;
         va_copy (xp, ap);
         s->ptr += vsnprintf (s->query + s->ptr, s->len - s->ptr, fmt, xp);
         va_end (xp);
         // move pointer forward
         if (width < 0)
            (void) va_arg (ap, int);
         if (precision == -1)
            (void) va_arg (ap, int);
         if (strchr ("diouxXc", *f))
         {                      // int
            if (flaglong)
               (void) va_arg (ap, long);
            else if (flaglonglong)
               (void) va_arg (ap, long long);
            else
               (void) va_arg (ap, int);
         } else if (strchr ("eEfFgGaA", *f))
         {
            if (flaglongdouble)
               (void) va_arg (ap, long double);
            else
               (void) va_arg (ap, double);
         } else if (strchr ("s", *f))
         {
            char *a = va_arg (ap, char *);
            if (a && flagfree)
               free (a);
         } else if (strchr ("p", *f))
            (void) va_arg (ap, void *);
#endif
      }
      f++;
   }
   if (s->query)
      s->query[s->ptr] = 0;
}

void
sql_sprintf (sql_string_t * s, const char *f, ...)
{                               // Formatted print, append to query string
   va_list ap;
   va_start (ap, f);
   sql_vsprintf (s, f, ap);
   va_end (ap);
}

char *
sql_printf (char *f, ...)
{                               // Formatted print, return malloc'd string
   sql_string_t s = { 0 };
   va_list ap;
   va_start (ap, f);
   sql_vsprintf (&s, f, ap);
   va_end (ap);
   return s.query;
}

int
sql_colnum (SQL_RES * res, const char *fieldname)
{                               // Return row number for field name, -1 for not available. Case insensitive
   int n;
   if (!res || !res->fields)
      return -1;
   for (n = 0; n < res->field_count && strcasecmp (res->fields[n].name ? : "", fieldname); n++);
   if (n < res->field_count)
      return n;
   return -1;
}

char *
sql_col (SQL_RES * res, const char *fieldname)
{                               // Return current row value for field name, NULL for not available. Case insensitive
   if (!res || !res->current_row)
      return NULL;
   int n = sql_colnum (res, fieldname);
   if (n < 0)
      return NULL;
   return res->current_row[n];
}

SQL_FIELD *
sql_col_format (SQL_RES * res, const char *fieldname)
{                               // Return data type for column by name. Case insensitive
   if (!res || !res->current_row)
      return NULL;
   int n = sql_colnum (res, fieldname);
   if (n < 0)
      return NULL;
   return &res->fields[n];
}

#ifndef LIB
int
main (int argc, const char *argv[])
{
   char *x = malloc (10001);
   int i;
   for (i = 0; i < 10000; i++)
      x[i] = '\'';
   x[i] = 0;
   char *q = sql_printf ("Testing %d %d %d %B %#B %T %c %#c %c %#c %s %#s %#s %#s %#10s %#-10s %#10.4s %#s %#s",
                         1, 2, 3, 1, 1, time (0),
                         'a',
                         'a',
                         '\'', '\'', "test",
                         "test2", "te'st3", "te\\st4", "test5", "test6", "test7", (void *) 0, x);
   puts (q);
   return 0;
}
#endif

//--------------------------------------------------------------------------------

// DEPRECATED - PLEASE USE sql_print INSTEAD
// returns next (at null)
char *
sqlprintf (char *q, char *f, ...)       // DEPRECATED
{                               // DEPRECATED
   va_list ap;

   va_start (ap, f);
   while (*f)
   {
      if (*f == '%')
         switch (*++f)
         {
         case 'l':             /* literal */
            {
               char *i = va_arg (ap, char *);

               while (*i)
                  *q++ = *i++;
            }
            break;
         case 'S':             /* sql string  un quoted */
         case 's':             /* sql string */
            {
               char *i = va_arg (ap, char *);

               if (i)
               {
                  if (*f == 's')
                     *q++ = '\'';
                  while (*i)
                  {
                     if (*i == '\n')
                     {
                        *q++ = '\\';
                        *q++ = 'n';
                        i++;
                        continue;
                     }
                     if (*i == '\\' || *i == '\'' || *i == '"')
                        *q++ = '\\';    // escape
                     *q++ = *i++;
                  }
                  if (*f == 's')
                     *q++ = '\'';
               } else
               {
                  *q++ = 'N';
                  *q++ = 'U';
                  *q++ = 'L';
                  *q++ = 'L';
               }
            }
            break;
         case 'i':             /* sql string of IP address */
            {
               unsigned int i = va_arg (ap, unsigned int);

               sprintf (q, "'%d.%d.%d.%d'", i >> 24, i >> 16 & 255, i >> 8 & 255, i & 255);
               q += strlen (q);
            }
            break;
         case 'd':             /* sql decimal */
            {
               int d = va_arg (ap, int);

               sprintf (q, "%d", d);
               q += strlen (q);
            }
            break;
         case 'D':             /* sql long long decimal */
            {
               long long d = va_arg (ap, long long);

               sprintf (q, "%lld", d);
               q += strlen (q);
            }
            break;
         case 'u':             /* sql decimal unsigned */
            {
               unsigned int d = va_arg (ap, unsigned int);

               sprintf (q, "%u", d);
               q += strlen (q);
            }
            break;
         case 'U':             /* sql long long decimal unsigned */
            {
               unsigned long long d = va_arg (ap, unsigned long long);

               sprintf (q, "%llu", d);
               q += strlen (q);
            }
            break;
         case 't':             /* sql time - assume UTC not local */
            {
               time_t t = va_arg (ap, time_t);

               if (t)
               {
                  struct tm *tm = gmtime (&t);

                  sprintf (q, "%04d%02d%02d%02d%02d%02d", tm->tm_year + 1900,
                           tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec);
                  q += strlen (q);
               } else
                  *q++ = '0';
            }
            break;
         case 'y':             /* sql Y/N string */
            {
               char c = va_arg (ap, int);

               *q++ = '\'';
               *q++ = (c ? 'Y' : 'N');
               *q++ = '\'';
            }
            break;
         case 'c':             // char
            {
               char c = va_arg (ap, int);
               if (c == '\'')
               {
                  *q++ = '"';
                  *q++ = c;
                  *q++ = '"';
               } else
               {
                  *q++ = '\'';
                  *q++ = c;
                  *q++ = '\'';
               }
            }
            break;
         default:
            *q++ = *f;
      } else
         *q++ = *f;
      f++;
   }
   va_end (ap);
   *q = 0;
   return q;
}

time_t
sql_time_z (const char *t, int utc)
{                               // time_t for sql date[time], -1 for error, 0 for 0000-00-00 00:00:00
   if (!t)
      return -1;
   unsigned int Y = 0,
      M = 0,
      D = 0,
      h = 0,
      m = 0,
      s = 0,
      c;
   c = 4;
   while (isdigit (*t) && c--)
      Y = Y * 10 + *t++ - '0';
   if (*t == '-')
      t++;
   c = 2;
   while (isdigit (*t) && c--)
      M = M * 10 + *t++ - '0';
   if (*t == '-')
      t++;
   c = 2;
   while (isdigit (*t) && c--)
      D = D * 10 + *t++ - '0';
   if (*t == ' ' || *t == 'T')
      t++;
   if (*t)
   {                            // time
      c = 2;
      while (isdigit (*t) && c--)
         h = h * 10 + *t++ - '0';
      if (*t == ':')
         t++;
      c = 2;
      while (isdigit (*t) && c--)
         m = m * 10 + *t++ - '0';
      if (*t == ':')
         t++;
      c = 2;
      while (isdigit (*t) && c--)
         s = s * 10 + *t++ - '0';
      if (*t == '.')
      {                         // fractions - skip over
         t++;
         while (isdigit (*t))
            t++;
      }
   }
   if (!Y && !M && !D && !h && !m && !s)
      return 0;                 // sql time 0000-00-00 00:00:00 is returned as 0 as special case
   if (!Y || !M || !D || M > 12 || D > 31)
      return -1;                // mktime does not treat these as invalid - we should for SQL times
 struct tm tm = { tm_year: Y - 1900, tm_mon: M - 1, tm_mday: D, tm_hour: h, tm_min: m, tm_sec:s };
   if (*t == 'Z' || utc)
      return timegm (&tm);      // UTC
   tm.tm_isdst = -1;            // Work it out
   return mktime (&tm);         // local time
}

void
sql_transaction (SQL * sql)
{
   sql_safe_query (sql, "START TRANSACTION");
}

int __attribute__((warn_unused_result)) sql_commit (SQL * sql)
{
   return sql_query (sql, "COMMIT");
}

void
sql_safe_commit (SQL * sql)
{
   return sql_safe_query (sql, "COMMIT");
}

void
sql_safe_rollback (SQL * sql)
{
   return sql_safe_query (sql, "ROLLBACK");
}
