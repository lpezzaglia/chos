/*
 * CHOS (c) 2004, The Regents of the University of California, through
 * Lawrence Berkeley National Laboratory (subject to receipt of any
 * required approvals from the U.S. Dept. of Energy).  All rights
 * reserved.
 *
 * If you have questions about your rights to use or distribute this
 * software, please contact Berkeley Lab's Technology Transfer
 * Department at  TTD@lbl.gov referring to "CHOS (LBNL Ref CR-2025)"
 *
 * NOTICE.  This software was developed under funding from the U.S.
 * Department of Energy.  As such, the U.S. Government has been granted
 * for itself and others acting on its behalf a paid-up, nonexclusive,
 * irrevocable, worldwide license in the Software to reproduce, prepare
 * derivative works, and perform publicly and display publicly.
 * Beginning five (5) years after the date permission to assert
 * copyright is obtained from the U.S. Department of Energy, and subject
 * to any subsequent five (5) year renewals, the U.S. Government is
 * granted for itself and others acting on its behalf a paid-up,
 * nonexclusive, irrevocable, worldwide license in the Software to
 * reproduce, prepare derivative works, distribute copies to the public,
 * perform publicly and display publicly, and to permit others to do so.
 *
 */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <linux/sched.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>

#include "../config.h"
#include "../chos.h"


chos_env *env_root = NULL;

void chos_debug(char *msg, ...) {
    va_list argv;
    va_start(argv,msg);
    /* vsyslog(LOG_ERR,msg,argv); */
    if(chos_debug_flag) 
      vfprintf(stderr,msg,argv);
      /* chos_err(msg,argv); */
    va_end(argv);
}

void chos_err(char *msg, ...) {
    va_list argv;
    va_start(argv,msg);
    /* vsyslog(LOG_ERR,msg,argv); */
    vfprintf(stderr,msg,argv);
    va_end(argv);
}

int configure_chos() {
  DIR *dir;
  struct dirent *dirent;
  unsigned char *type;

  chos_debug("Info: Initializing CHOS\n");

  if(!(env_root = chos_create_env("root"))) {
    chos_err("Failed to initialize CHOS.\n");
    return 0;
  }


  dir = opendir(chos_config_prefix);

  if(!dir) {
    chos_err("Failed to opendir %s: %s.\n",chos_config_prefix, strerror(errno));
    return 0;
  }

  while(1) {
    dirent = readdir(dir);

    if(!dirent) {
      chos_debug("Info: Done reading configs\n");
      break;
    }

    if(dirent->d_type == DT_REG || dirent->d_type == DT_UNKNOWN) {
      chos_debug("Info: Reading config %s.\n",dirent->d_name);

      if(chos_append_env(dirent->d_name)!=0) {
        chos_err("Failed to add environment \"%s\" to CHOS.\n",dirent->d_name);
        return 1;
      }

    }
    else {
      chos_debug("Info: Skipping file %s.\n",dirent->d_name);
    }
  }

  return 1;
}

int chos_append_env(char *name) {
  chos_env *env = env_root;
  while(env->next) {
    env = env->next;
  }
  if(!(env->next = chos_create_env(name))) {
      chos_err("Failed to build environment %s.\n",name);
      return 1;
  }
  return 0;
}

int chos_append_dir(chos_env *env, char *src, char *dest) {
  chos_dir *dir = env->dirs;
  chos_debug("Info: chos_append_dir src=%s dest=%s env=%s.\n",src,dest,env->name);

  if(!dir) {
    env->dirs = chos_create_dir(src,dest,NULL);
    return 0;
  }

  while(dir->next!=NULL) {
    dir = dir->next;
  }

  dir->next = chos_create_dir(src,dest,dir);
  if(dir->next) {
    return 0;
  }
  else {
    return 1;
  }
}

chos_dir *chos_create_dir(char *src, char *dest, chos_dir *prev) {
  chos_debug("Info: chos_create_dir src=%s dest=%s\n",src,dest);
  chos_dir *dir = (chos_dir *)malloc(sizeof(chos_dir));

  dir->next = NULL;
  dir->prev = prev;

  dir->src = (char *)malloc(sizeof(char)*(strlen(src)+1));
  strncpy(dir->src,src,strlen(src)+1);

  dir->dest = (char *)malloc(sizeof(char)*(strlen(dest)+1));
  strncpy(dir->dest,dest,strlen(dest)+1);


  return dir;
}


chos_env *chos_create_env(char *name) {

  int conf_path_len=strlen(chos_config_prefix)+strlen(name);

  chos_debug("Info: Creating environment %s.\n",name);

  chos_env *env = (chos_env *)malloc(sizeof(chos_env));

  env->name = (char *)malloc(sizeof(char)*(strlen(name)+1));
  strncpy(env->name, name, strlen(name)+1);

  env->config_file = (char *)malloc(sizeof(char)*(conf_path_len+1));
  strncpy(env->config_file, chos_config_prefix, conf_path_len+1);
  strncat(env->config_file, name, strlen(name));

  env->dirs=NULL;

  if(!(chos_populate_dirs(env))) {
    chos_err("Failed to read configuration for environment \"%s\".\n",env->name);
    return NULL;
  }

  env->next = NULL;
  return env;
}

/* Check to see if we are already chrooted.  This is an indirect test.
 * We look to see if /etc/chos.d/CURRENT exists.
 */
int is_chrooted(void) {
    return(chos_get_env("CURRENT")->dirs == NULL);
}

/* Return the environment with name env_name,
 * or NULL if no such environment exists.
 */
chos_env *chos_get_env(char *env_name) {
    chos_env *env = env_root;

    do {
        if (strcmp(env_name,env->name)==0) {
            return env;
        }
        else {
            chos_debug("Info: Environment %s did not match %s.\n",env->name, env_name);
        }
        env = env->next;
    } while (env != NULL);

    return env;
}


/* Set the CHOS environment
 */
int set_multi(char *os)
{

  if(!os) {
    chos_err("Invalid environment name.\n");
    return(1);
  }

  if(!env_root) {
    chos_err("CHOS not configured (did you run configure_chos()?)\n");
    return(1);
  }

  chos_env *env = chos_get_env(os);

  if(!env) {
    chos_err("Environment %s not found.\n",os);
    return(1);
  }

  if(unshare(CLONE_NEWNS)!=0) {
    perror("clone");
    return(1);
  }


  if(is_chrooted()==0) {

    if(chdir("/local")!=0) {
      chos_err("chdir: %s\n",strerror(errno));
      return 1;
    }

    if(pivot_root("/local","/local/chos2")!=0) {
      chos_err("pivot_root: %s\n",strerror(errno));
      return 1;
    }

    if(chdir("/")!=0) {
      chos_err("chdir: %s\n",strerror(errno));
      return 1;
    }

    if(umount_dirs(chos_get_env("CURRENT"))!=0) {
      chos_err("umount_dirs: %s\n",strerror(errno));
      return 1;
    }
  }

  if(env->dirs!=NULL) {
    if(mount_dirs(env,chos_root)!=0) {
      perror("mount_dirs");
      return 1;
    }

    if(chdir("/chos2")!=0) {
      chos_err("chdir: %s\n",strerror(errno));
      return 1;
    }

    if(pivot_root("/chos2","/chos2/local")!=0) {
      chos_err("pivot_root: %s\n",strerror(errno));
      return 1;
    }


    chos_debug("Info: chroot complete env=%s chos_root=%s\n",env->name,chos_root);
  }

  return 0;
}

int umount_dirs(chos_env *env) {
  chos_dir *dir = env->dirs;
  while(dir->next) {
    dir = dir->next;
  }
  while(dir) {
    if(strncmp("/chos2/local",dir->dest,12)!=0) {
      if(umount(dir->dest)!=0) {
        chos_err("umount %s: %s\n",dir->dest,strerror(errno));
        return 1;
      }
        chos_debug("Unmounted %s\n",dir->dest);
    }
    else {
      chos_debug("Skipping %s\n",dir->dest);
    }
    dir = dir->prev;
  }
  return 0;
}

int mount_dirs(chos_env *env, const char *chos_root) {
  chos_debug("Info: enter mount_dirs env=%s chos_root=%s\n",env->name,chos_root);
  chos_dir *dir = env->dirs;

  while(dir) {
    char *src = dir->src;
    char *dest = dir->dest;

    chos_debug("Info: %s on %s\n",src,dest);

    /* TODO: MS_NODEV and MS_NOSUID do not appear to work here */
    if(mount(src,dest,"bind", MS_BIND|MS_NODEV|MS_NOSUID|MS_RDONLY,NULL)!=0) {
       chos_err("mount %s on %s: %s\n",src,dest,strerror(errno));
       return 1;
     }
    chos_debug("Info: Completed %s on %s\n",src,dest);

    dir = dir->next;
  }

  return 0;
}


/*
 * This function looks at the /etc/chos.conf file and sets variables
 * listed in the %ENV section based on the environment of the calling shell.
 */
char ** set_env()
{
  FILE *stream;
  char **env;
  char *value;
  char *var;
  char buffer[MAXLINE];
  int ret=0;
  int count=1;
  int setc=0;
  int start=0;
  int len;

  stream=fopen(CHOSENV,"r");
  if (!stream){

    fprintf(stderr,"Unable to open conf file (%s)\n",CHOSENV);
    perror("chos.conf");
    return NULL; 
  }

/* We need to count the number of variables first. */
  start=0;
  while(1){
    value=fgets(buffer,MAXLINE,stream);
    if (value==NULL)
      break;
    if (buffer[0]=='#' || buffer[0]=='\n')
      continue; 
/* Remove new line */
    while(*value!=0 && *value!='\n')
    value++;
    *value=0;
    if (start){
      count++;
      if (buffer[0]=='%')
  break;
    }
    else if (strcmp(buffer,ENVHEAD)==0){
      start=1;
    }
  }

/* Allocate an array of pointers */
  env=(char **)malloc(sizeof(char *)*count);
  if (env==NULL){
    fprintf(stderr,"Failed to allocate memory for env\n");
    return NULL;
  }
  rewind(stream);
  start=0;
  while(1){
    value=fgets(buffer,MAXLINE,stream);
    if (value==NULL)
      break;
    if (buffer[0]=='#' || buffer[0]=='\n')
      continue; 
/* Remove new line */
    while(*value!=0 && *value!='\n')
    value++;
    *value=0;
    if (start){
      count++;
      if (buffer[0]=='%')
    break;
      value=getenv(buffer);
      if (strcmp(buffer,"PATH")==0){
        env[setc]=DEFPATH;
        setc++;
      }
      else if (value!=NULL && setc<count){
        len=strlen(buffer)+strlen(value)+2;
        var=(char *)malloc(len);     /* Allocate space for entry */
        if (var==NULL){
          fprintf(stderr,"failed to allocate memory\n");
          return;
        }
/*        printf("%s=%s\n",buffer,value); */
        sprintf(var,"%s=%s",buffer,value);
        env[setc]=var;
        setc++;
      }
/*      else{
 *       fprintf(stderr,"var %s not set\n",buffer);
 *     }
 */
    }
    else if (strcmp(buffer,ENVHEAD)==0){
      start=1;
    }
  }
  if (setc<count){
    env[setc]=NULL;
  }
  else{
    fprintf(stderr,"Unable to terminate environment\n");
    return NULL;
  }
  fclose(stream);
  return env; 
}

/* This looks up the path for the alias specified in the users chos file.
 */
char *check_chos(char *name)
{
  FILE *cfile;
  static char buffer[MAXLINE];
  struct stat st;
  char *path;
  char *retpath=NULL;
  int start=0;
  int han;

  cfile=fopen(CHOSCONF,"r");
  if (cfile==NULL){
    chos_err("Error opening config file %s\n",CHOSCONF);
    return NULL;
  }
  han=fileno(cfile);
  if (fstat(han,&st)!=0){
    chos_err("Error accessing config file %s\n",CHOSCONF);
    return NULL;
  }
  else if (st.st_uid!=0){
    chos_err("Error: %s must be owned by root\n",CHOSCONF);
    return NULL;
  }
  start=0;
  while(retpath==NULL){
    path=fgets(buffer,MAXLINE,cfile);
    if (path==NULL)
      break;
    if (buffer[0]=='#' || buffer[0]=='\n')
      continue;
/* Remove new line */
    while(*path!=0 && *path!='\n')
        path++;
    *path=0;
    if (start){
      if (buffer[0]=='%')
        break;
      path=buffer;
      while (*path!=':' && *path!=0){
        path++;
      }
      if (*path==0){
/*        fprintf(stderr,"Invalid line in chos config file: %s.\n",buffer); */
        continue;
      }
      *path=0;
      path++;
      if (strcmp(buffer,name)==0){
        retpath=path;
        break;
      }
    }
    else if (strcmp(buffer,SHELLHEAD)==0){
      start=1;
    }
  }
  fclose(cfile);
  return retpath;
}

int chos_populate_dirs(chos_env *env)
{
  FILE *cfile;
  static char buffer[MAXLINE];
  char *src;
  char *dest;
  struct stat st;
  char *line;
  const char delim[] = " ";
  int han;

  chos_debug("Info: in chos_populate_dirs env=%s.\n",env->name);

  cfile=fopen(env->config_file,"r");
  if (cfile==NULL){
    chos_err("Error opening config file %s\n",env->config_file);
    return 0;
  }
  han=fileno(cfile);
  if (fstat(han,&st)!=0){
    chos_err("Error accessing config file %s\n",env->config_file);
    return 0;
  }
  else if (st.st_uid!=0){
    chos_err("Error: %s must be owned by root\n",env->config_file);
    return 0;
  }
  while(line=fgets(buffer,MAXLINE,cfile)) {
    //if (path==NULL)
     // break;
    if (buffer[0]=='#' || buffer[0]=='\n')
      continue;

    /* Strip new line character */
    while(*line!=0 && *line!='\n')
        line++;
    *line=0;


    src = strtok(buffer,delim);
    dest = strtok(NULL,delim);

    if(!src || !dest) {
      chos_err("Invalid line in chos config file: %s.\n",buffer);
      return 0;
    }

    chos_debug("Info: Append src=%s dest=%s env=%s.\n",src,dest,env->name);
    chos_append_dir(env, src, dest);
  }

  fclose(cfile);

  return 1;
}

int read_chos_file(char *dir,char *osenv,int max)
{
  int conf;
  int i;
  int count=0;
  char userfile[MAX_LEN+1];

  sprintf(userfile,"%.100s/%.5s",dir,CONFIG);
  conf=open(userfile,O_RDONLY);
  if (conf>=0){
    count=read(conf,osenv,MAXLINE);
    osenv[count]=0;
    close(conf);
  }
  if (count==0){
    strcpy(osenv,DEFAULT);
    count=7;
  }

  for (i=0;i<count;i++){
    if (osenv[i]=='\n')break;
  }
  osenv[i]=0;
  return count;
}
