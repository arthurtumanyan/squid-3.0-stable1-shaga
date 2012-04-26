/*
 * AUTHOR: Arthur Tumanyan 
 *
 * SQUID Web Proxy Cache          http://www.squid-cache.org/
 * ----------------------------------------------------------
 *
 *  Squid is the result of efforts by numerous individuals from
 *  the Internet community; see the CONTRIBUTORS file for full
 *  details.   Many organizations have provided support for Squid's
 *  development; see the SPONSORS file for full details.  Squid is
 *  Copyrighted (C) 2001 by the Regents of the University of
 *  California; see the COPYRIGHT file for full details.  Squid
 *  incorporates software developed and/or copyrighted by other
 *  sources; see the CREDITS file for full details.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111, USA.
 *
 */

#include "squid.h"
#include "shaga.h"
//
int db_connect(char *dbhost,char *dbuser,char *dbpwd,char *dbname,unsigned int dbport){ 

    mysql_init(&mysql);
    connection = mysql_real_connect(&mysql,dbhost,dbuser,dbpwd,dbname,dbport,NULL,0);
    /* check for connection error */
    if(connection == NULL) {
        /* log the error message */
     snprintf(MYSQL_ERR,LINE_MAXLEN,"%s\n",(char *)mysql_error(&mysql)); 
	_db_print(MYSQL_ERR);
       /* log this */ 

        return 1;
    }

  return 0; 
}
void init_shaga_support(){

if ( db_connect(Config.Shaga.db_host,
		Config.Shaga.db_user,
		Config.Shaga.db_pwd,
		Config.Shaga.db_name,
		Config.Shaga.db_port) != 0)
 {
        snprintf(MYSQL_ERR,LINE_MAXLEN,"Cannot connect to database %s on %s\n", 
                                            Config.Shaga.db_name,Config.Shaga.db_host);
	/* log this*/if(!already_logged){	
	_db_print(MYSQL_ERR);
	already_logged = 1;
 }
	_db_print("ShagaEngine can not start\n");
	_db_print("Fix all errors\n");
	shaga_force = 1;
}else {
	exclude = (0 == getExcludeHosts()) ? 1 : 0;
	_db_print("ShagaEngine is started\n");
	snprintf(TMP_MSG,LINE_MAXLEN,"Exclude hosts detected %d\n",hc);
	_db_print(TMP_MSG);

}
}

//
int writeToDB(char * __time,char * username,char * ip_address,char * byte){
	
  long int my_stamp = atol(__time);	
  time_t my_stamp2 = (time_t)my_stamp;

	thistime = localtime(&my_stamp2);
	strftime(timestr,25,"%F %T",thistime);
	int retval;
	
	snprintf(TMP_QUERY,LINE_MAXLEN,
		 "INSERT DELAYED INTO %s(`date`,`user`,`ip_addr`,`bytes`) VALUES(\'%s\',\'%s\',\'%s\',\'%s\')",
		TMP_TABLE,(char *) timestr,username,(char *)ip_address,(char *)byte);
if(shaga_force == 0){
if(exclude == 1 && ( 0 == isExHost(ip_address) ) ){	
	state = mysql_real_query(&mysql,TMP_QUERY,strlen(TMP_QUERY));
	
} /* IP address is from exclude hosts pool */
	} /* connection failed */

	//
	if(state) {
		snprintf(MYSQL_ERR,LINE_MAXLEN,"%s\n",(char *)mysql_error(&mysql));
	 /* log this */	
if(!already_logged){	
	_db_print(MYSQL_ERR);
	already_logged = 1;
 }
		return 1; 
	}
	int affected = (int)mysql_affected_rows(&mysql);
	if(affected > 0){
		retval = 0;
	}else{ 
		snprintf(MYSQL_ERR,LINE_MAXLEN,"%s\n",(char *)mysql_error(&mysql));
	/* log this */	
	if(!already_logged) {
		 _db_print(MYSQL_ERR); 
		already_logged = 1;
	}
		retval = 1;
	}

	return retval;
}
//

void shutdown_shaga_support(){
	if (shaga_force == 0)_db_print("ShagaEngine is shutting down\n");
	if(connection != NULL) mysql_close(&mysql);
}

int getExcludeHosts(){
        //
        snprintf(TMP_QUERY,LINE_MAXLEN,"%s",
                 "SELECT `hosts` FROM `ecl_hosts`");
        state = mysql_real_query(&mysql,TMP_QUERY,strlen(TMP_QUERY));
        //
        if(state)
        {
            snprintf(MYSQL_ERR,LINE_MAXLEN,"%s\n",(char *)mysql_error(&mysql));
	_db_print(MYSQL_ERR);
        /* log about failure */    
	return 1;
        }
        result = mysql_store_result(&mysql);
        if (result)  // there are rows
        { 
	int c = 0;
            while((row = mysql_fetch_row(result)))
            {
                sscanf(row[0],"%s",excludeHosts[c].host);
		
		c++;
}
            mysql_free_result(result);
	hc = c;
            //
        }else
        {

 snprintf(MYSQL_ERR,LINE_MAXLEN,"Error getting records: %s\n",(char *) mysql_error(&mysql));
	_db_print(MYSQL_ERR);
       /* log about failure */ 
        }
	return 0;
}

int isExHost(char *host){

int i,ret = 0;

if(strlen(host) < 7) return 0;
    for(i = 0;i < hc;i++)
    { 
	if(strncmp(host,excludeHosts[i].host,15) == 0)
        {
                ret = 1;
            break;
        }//strcmp
    }//for
    return ret;
}
//
//

