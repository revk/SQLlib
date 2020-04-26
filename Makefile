ifneq ($(wildcard /usr/bin/mysql_config),)
	SQLINC=$(shell mysql_config --include)
	SQLLIB=$(shell mysql_config --libs)
	SQLVER=$(shell mysql_config --version | sed 'sx\..*xx')
endif
ifneq ($(wildcard /usr/bin/mariadb_config),)
	SQLINC=$(shell mariadb_config --include)
	SQLLIB=$(shell mariadb_config --libs)
	SQLVER=$(shell mariadb_config --version | sed 'sx\..*xx')
endif

sqllib.o: sqllib.c Makefile
	cc -g -O -c -o $@ $< -D_GNU_SOURCE -DLIB ${SQLINC} -DMYSQL_VERSION=${SQLVER}

