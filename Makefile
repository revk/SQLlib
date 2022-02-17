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
OPTS=-D_GNU_SOURCE --std=gnu99 -g -Wall -funsigned-char -lpopt

all: sqllib.o sqlexpand.o sql sqlwrite sqledit sqlexpand

sqllib.o: sqllib.c sqllib.h Makefile
	gcc -g -O -c -o $@ $< -fPIC ${OPTS} -DLIB ${SQLINC} -DMYSQL_VERSION=${SQLVER}

sql: sql.c sqllib.o sqllib.h sqlexpand.o sqlexpand.h
	gcc -g -O -o $@ $< -fPIC ${OPTS} -DNOXML ${SQLINC} ${SQLLIB} sqllib.o sqlexpand.o -lcrypto -luuid

sqlwrite: sqlwrite.c sqllib.o sqllib.h
	gcc -g -O -o $@ $< -fPIC ${OPTS} ${SQLINC} ${SQLLIB} sqllib.o

sqledit: sqledit.c sqllib.o sqllib.h
	gcc -g -O -o $@ $< -fPIC ${OPTS} ${SQLINC} ${SQLLIB} sqllib.o

sqlexpand.o: sqlexpand.c Makefile
	cc -c -o $@ $< ${OPTS} -DLIB

sqlexpand: sqlexpand.c Makefile
	cc -O -o $@ $< ${OPTS} -luuid -lcrypto
