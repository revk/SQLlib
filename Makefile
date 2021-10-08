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
all: sqllib.o sql sqlwrite sqledit

sqllib.o: sqllib.c sqllib.h Makefile
	gcc -g -O -c -o $@ $< -fPIC -D_GNU_SOURCE -DLIB ${SQLINC} -DMYSQL_VERSION=${SQLVER} --std=gnu99

sql: sql.c sqllib.o sqllib.h
	gcc -g -O -o $@ $< -fPIC -D_GNU_SOURCE ${SQLINC} ${SQLLIB} -lpopt sqllib.o --std=gnu99

sqlwrite: sqlwrite.c sqllib.o sqllib.h
	gcc -g -O -o $@ $< -fPIC -D_GNU_SOURCE ${SQLINC} ${SQLLIB} -lpopt sqllib.o --std=gnu99

sqledit: sqledit.c sqllib.o sqllib.h
	gcc -g -O -o $@ $< -fPIC -D_GNU_SOURCE ${SQLINC} ${SQLLIB} -lpopt sqllib.o --std=gnu99
