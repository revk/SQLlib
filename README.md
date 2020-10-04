# SQLlib
Wrapper / library for mysql, including sql query formatting, functions for safe operation that exit if unexpected failure, and functions to access results of queries by named field rather than row[n].

The underlying mysql client functions need some help.

This wrapper provides a range of functions - some just access to the mysql functions and some provide a lot of extra functionailty.

The main ones are variations of the simple sql_query, which can be "safe" meaning it aborts on error.
The "free" option on these frees the argument, and is used with sql_printf() which returned a malloc'd string allowing formatting of SQL query with proper escaping.

This allows things like

sql_safe_query_free(&sql,sql_printf("INSERT INTO \`test\` SET \`f1\`=%#s",stringvar));

This would create a queries, properly quoting and escaping the value of stringvar, even using NULL if it is null, and then free the allocated string. If there is an error the program exists.

There are also debug settings allowing you to see every query that is done, to stderr or syslog.
