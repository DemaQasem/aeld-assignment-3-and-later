#include <string.h>
#include <stdio.h>
#include <syslog.h>
#include <errno.h>


int retValue_fclose,retValue_fopen,retValue_fprintf,Current_errno;
int main(int argc, char *argv[])
{
  	openlog("writer", LOG_PID, LOG_USER);

	if(argc != 3)//if arguments are not 3 (filename, writefile, writestr) then : error
	{
	  syslog(LOG_ERR,"Invalid Number of Arguments");
	  closelog();
	  return 1;
	}
	//argv[1] : path of a file
	//argv[2] : string to be written to that file

	const char *writestr = argv[2];
	const char *writefile = argv[1];
	
	FILE *file = fopen(writefile, "w");
	Current_errno = errno;
	if(file == NULL)
	{
	  syslog(LOG_ERR, "Error %s, Failed to open file :%s", strerror(Current_errno),writefile);
	  closelog();
	  return 1;
	}

	retValue_fprintf = fprintf(file, "%s", writestr);
	Current_errno = errno;
	
	if(retValue_fprintf < 0)//the fprintf returns a negative value upon failure
	{
	  syslog(LOG_ERR, "Error %s, failed to write to file %s", strerror(Current_errno) ,writefile);
	  
	  retValue_fclose = fclose(file);
	  Current_errno = errno;
	  if(retValue_fclose == EOF)
	  {
	    syslog(LOG_ERR, "Error %s, failed to close the file %s", strerror(Current_errno) ,writefile);
	    closelog();
	    return 1;
	  }
	  
	  
	  closelog();
	  return 1;
	}
	else 
	{
	  syslog(LOG_DEBUG,"Writing %s to file %s",writestr,writefile);

	  retValue_fclose = fclose(file);
	  Current_errno = errno;
	  if(retValue_fclose == EOF)
	  {
	    syslog(LOG_ERR, "Error %s, failed to close the file %s", strerror(Current_errno) ,writefile);
	    closelog();
	    return 1;
	  }
	  
	  closelog();
 	  syslog(LOG_DEBUG,"Done writing successfully");
	  return 0;
	}
	
}
