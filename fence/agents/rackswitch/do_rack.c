#include "do_rack.h"

char *pname = "fence_rack";

int quiet_flag = 0;
int verbose_flag = 0;
int debug_flag = 0;

char ipaddr[256];
char portnumber[256];
char username[256];
char password[256];
char arg[256];
char name[256];
char pwd_script[PATH_MAX] = { 0, };

char readbuf[MAXBUF];
char writebuf[MAXBUF];
int sock;

char op_login = 0x7e; 	        /* 126*/ 
char op_action = 0x66;	        /* 102 */
char ack_login = 0x7D;	        /* 125 */

char action_idle = 0x00;
char action_reset = 0x01;
char action_off = 0x02;
char action_offon = 0x03;

char configuration_request = 0x5b;   /* 91 */
char config_reply = 0x5c;            /* 92 */
char config_general = 0x01;
char config_section1 = 0x02;
char config_section2 = 0x03;
char config_section3 = 0x04;

char message_status = 0x65;         /* 101 */

char login_deny = 0xFF;          

int time_out = 60;

void ignore_message_status(void);
int wait_frame(char);

/*
 * scan input, waiting for a given frame
 */
int wait_frame(char frame_id)
{
  int read_more = 1;
  int success = 0;
  int n;
  char target = frame_id;

  if(debug_flag){printf("%s: Looking for frametype 0x%.2x\n",name,target);}
  read_more = 1;
  while(read_more){
    n = read(sock,readbuf,1);
    if(debug_flag){printf("%s: Found frametype 0x%.2x\n",name,readbuf[0]);}
    if(readbuf[0] == target){
      read_more = 0;
      success = 1;
    }
    else{
      if(readbuf[0] == message_status){
	ignore_message_status();
	read_more = 1;
      }
      else{
	if(debug_flag){printf("%s: Got unexpected frame from switch\n",name);}
	read_more = 0;
	success = 0;
      }
    }
  }
  return(success);
}  

void ignore_message_status(void)
{
  int n,i;
  int read_more = 1;
  int number_of_temp;
  int number_of_config_mobo;
    
  if(debug_flag){printf("%s: Ignoring message-status\n",name);}
  read_more=1;
  while(read_more){
    n = read(sock,readbuf,1); /* status */
    if(n == 1)
      read_more = 0;
  }

  read_more = 1;
  while(read_more){         /* Date & time */
    n = read(sock,readbuf,1);
    if(readbuf[0] == '\0'){
      read_more = 0;
    }
    else{
      read_more = 1;
    }
  }
   
  read_more = 1;
  number_of_temp = 0;
  n = read(sock,readbuf,1); /* Temprature Input count */
  number_of_temp = (int)readbuf[0];
  
  for(i=0;i<number_of_temp;i++){
    read_more = 1;
    while(read_more){
      n = read(sock,readbuf,1); /* Temprature input ID */
      n = read(sock,readbuf,8); /* Temprature Value, Fahrenheit */
      n = read(sock,readbuf,8); /* Temprature Vaule, Celcius */
      n = read(sock,readbuf,1); /* Temprature Alarm */
    }
  }
  number_of_config_mobo = 0;
  for(i=4;i>0;i--){
    read_more = 1;	
    while(read_more){
      n=read(sock,readbuf,1);
      if(n == 1){
	read_more = 0;
	number_of_config_mobo = number_of_config_mobo + (int)(readbuf[0]<<(8*(i-1)));
      }
    }
  }
  for(i=0;i<number_of_config_mobo;i++){
    n = read(sock,readbuf,4); /* Motherboard ID */
    n = read(sock,readbuf,1); /* Motherboard status */
  }
}



void print_usage(void)
{
  printf("Usage:\n");
  printf("\n");
  printf("%s [options]\n"
         "\n"
         "Options:\n"
         "  -h               usage\n"
	 "  -a <ip>          IP address for RackSwitch\n"
	 "  -n <dec num>     Physical plug number on RackSwitch\n"
	 "  -l <string>      Username\n"
	 "  -p <string>      Password\n"
	 "  -S <path>        Script to retrieve password\n"
	 "  -v               Verbose\n"
	 "  -q               Quiet\n"
         "  -V               Version information\n", pname);
}



void get_options(int argc, char **argv)
{
  int c;
  char *value;

  if (argc > 1){  
    /*
     * Command line input
     */
    while ((c = getopt(argc, argv, "ha:n:l:p:S:vqVd")) != -1)
      {
	switch(c)
	  {
	  case 'h':
	    print_usage();
	    exit(DID_SUCCESS);
	
	  case 'a':
	    strncpy(ipaddr,optarg,254);
	    break;

	  case 'n':
	    strncpy(portnumber,optarg,254);
	    break;

	  case 'l':
	    strncpy(username,optarg,254);	
	    break;

	  case 'p':
	    strncpy(password,optarg,254);
	    break;

	  case 'S':
		strncpy(pwd_script, optarg, sizeof(pwd_script));
		pwd_script[sizeof(pwd_script) - 1] = '\0';
		break;

	  case 'v':
	    verbose_flag = 1;
	    break;

	  case 'q':
	    quiet_flag = 1;
	    break;

	  case 'd':
	    debug_flag = 1;
	    break;

	  case 'V':
	    printf("%s %s (built %s %s)\n", pname, RELEASE_VERSION,
		   __DATE__, __TIME__);
	    printf("%s\n", REDHAT_COPYRIGHT);
	    exit(DID_SUCCESS);
	    break;

	  case ':':
	  case '?':
	    fprintf(stderr, "Please use '-h' for usage.\n");
	    exit(DID_FAILURE);
	    break;

	  default:
	    fprintf(stderr, "Bad programmer! You forgot to catch the %c flag\n", c);
	    exit(DID_FAILURE);
	    break;

	  }
      }
    strcpy(name, pname);
  }
  else{
    errno = 0;
    while(fgets(arg, 256, stdin) != NULL){    
 if( (value = strchr(arg, '\n')) == NULL){
        fprintf(stderr, "line too long: '%s'\n", arg);
	exit(DID_FAILURE);
      }
      *value = 0;
      if( (value = strchr(arg, '=')) == NULL){
        fprintf(stderr, "invalid input: '%s'\n", arg);
	exit(DID_FAILURE); 
      }   
      *value = 0;
      value++;
    /*  bahfuck. "agent" is not passed to us anyway
     *  if (!strcmp(arg, "agent")){
      *  strcpy(name, value);
       * pname = name;
      *}
      */
      strcpy(name, pname);
      if (!strcmp(arg, "ipaddr"))
        strcpy(ipaddr, value);
      
      if (!strcmp(arg, "portnumber"))
        strcpy(portnumber, value);
      
      if (!strcmp(arg, "username"))
        strcpy(username, value);
      
      if (!strcmp(arg, "password"))
        strcpy(password, value);

	  if (!strcasecmp(arg, "passwd_script")) {
		strncpy(pwd_script, optarg, sizeof(pwd_script));
		pwd_script[sizeof(pwd_script) - 1] = '\0';
	  }
    }
    errno = 0;
    
  }

  if (pwd_script[0] != '\0') {
	FILE *fp;
	char pwd_buf[1024];

	fp = popen(pwd_script, "r");
	if (fp != NULL) {
		ssize_t len = fread(pwd_buf, 1, sizeof(pwd_buf), fp);
		if (len > 0) {
			char *p;
			p = strchr(pwd_buf, '\n');
			if (p != NULL)
				*p = '\0';
			p = strchr(pwd_buf, '\r');
			if (p != NULL)
				*p = '\0';
			strncpy(password, pwd_buf, sizeof(password));
			password[sizeof(password) - 1] = '\0';
		}
		pclose(fp);
	}
  }
}

static void sig_alarm(int sig)
{
 if(!quiet_flag){
   fprintf(stderr,"failed: %s: Timeout, nothing happened for %d seconds.\n", pname, time_out);
   fprintf(stderr,"failed: %s: Perhaps you should inspect the RackSwitch at %s\n",pname,ipaddr);
 }
 exit(DID_FAILURE);	
}


int main(int argc, char **argv)
{
  int n,i,j,pnumb;
  int ip_portnumber = 1025;
  char boardnum = 0x00;
  /*char number_of_action = 0x01;*/
  int number_of_config_mobo = 0;
  int number_of_section_config_mobo = 0;
  int exit_status= 0;
  int success_off = 0;
  /*int success_on = 0;*/
  int read_more = 1;
  struct sockaddr_in rackaddr; 
  
  /*char mobo_enabled = 0x01;*/
  /*char mobo_default_status = 0x00;*/
  /*char mobo_output_status = 0x00;*/
  int this_mobo = 0;
  /*int mobo_id = 0;*/
  /*int our_mobo = 0;*/
  int number_of_temp = 0;

  memset(arg, 0, 256);
  memset(name, 0, 256);
  memset(ipaddr, 0, 256);
  memset(portnumber,0,256);
  memset(username,0,256);
  memset(password,0,256);

  /*
   * Ensure that we always get out of the fencing agent
   * even if things get fucked up and we get no replies
   */
  signal(SIGALRM, &sig_alarm);
  alarm(time_out);
  get_options(argc, argv);

  if(name[0] == '\0')
  {
    if(!quiet_flag)
      fprintf(stderr,"failed: no name for this program\n");
    exit(DID_FAILURE);
  }
  
  if(ipaddr[0] == '\0')
  {
    if(!quiet_flag)
      fprintf(stderr,"failed: %s, no IP address given\n",name);
    exit(DID_FAILURE);
  }
  if (portnumber[0] == '\0')
  {
    if(!quiet_flag)
      fprintf(stderr,"failed: %s, no portnumber given\n",name);
    exit(DID_FAILURE);
  }
  if (username[0] == '\0')
  {
    if(!quiet_flag)
      fprintf(stderr,"failed: %s, no username given\n",name);
    exit(DID_FAILURE);
  }

  if (password[0] == '\0')
  {
    if(!quiet_flag)
      fprintf(stderr,"failed: %s, no password given\n",name);
    exit(DID_FAILURE);
  }
  /*
   * Port number given to us as a string.
   * Does the number make sense?
   */
  pnumb = 0;
  for(n=0;(portnumber[n]!='\0');n++){
    if((portnumber[n] < 48) || (portnumber[n] > 57)){
      if(!quiet_flag)
	fprintf(stderr,"failed: %s, invalid port number\n",name);
      exit(1);
    }
    pnumb = ((pnumb * 10) + ((int)(portnumber[n]) - 48));
  }
  /*
   * what section of the rack is this port part of?
   * The switch has 4 "subsections", called boardnum here
   */
  if((pnumb > 0) && (pnumb < 47))
    boardnum = 0x02;
  if((pnumb > 46) && (pnumb < 94))
    boardnum = 0x03;
  if((pnumb > 93) && (pnumb < 137))
    boardnum = 0x04;
  if((pnumb < 1) || (pnumb> 136)){
    boardnum = 0x00;
    if(!quiet_flag)
      fprintf(stderr,"failed: %s, the portnumber given is not in the range [1 - 136]\n",name);
    exit(DID_FAILURE);
  } 
  /*********************************************
   ***
   *** set up TCP connection to the rackswitch
   ***
   ********************************************/
  if ((sock = socket(AF_INET,SOCK_STREAM,0)) < 0){
    fprintf(stderr,"failed: %s: socket error, %s\n",name,strerror(errno));
    exit(DID_FAILURE);
  }
  
  bzero(&rackaddr,sizeof(rackaddr));
  rackaddr.sin_family = AF_INET;
  rackaddr.sin_port = htons(ip_portnumber);

  if(inet_pton(AF_INET,ipaddr,&rackaddr.sin_addr) <= 0){
    fprintf(stderr,"failed: %s: inet_pton error\n", name);
  }
 
  if(connect(sock,(SA *) &rackaddr,sizeof(rackaddr)) < 0){
    fprintf(stderr,"failed: %s: connect error to %s, %s\n", name, ipaddr,strerror(errno));
    exit(DID_FAILURE);
  }
  /**********************************************
   ***
   ***	Send Login Frame
   ***
   *********************************************/
  writebuf[0] = op_login;
   
  for(n=0;n<=(strlen(username));n++){
    writebuf[sizeof(char)+n] = username[n];
  }
  writebuf[sizeof(char)+(strlen(username))+1] ='\n';
   
  for(n=0;n<=(strlen(password))+1;n++){
    writebuf[sizeof(char)+strlen(username)+1+n] = password[n];
  }
  writebuf[sizeof(char)+(strlen(username))+1+(strlen(password))+1] ='\n';
     
  if(write(sock,writebuf,sizeof(char)+strlen(username)+strlen(password)+2) < 0) {
    fprintf(stderr,"failed to write to socket\n");
    exit(DID_FAILURE);
  }
   
  /********************************************
   ***
   ***	Read Login Reply
   ***
   *******************************************/
 if(wait_frame(ack_login)){
   n=read(sock,readbuf,1);
   if(readbuf[0] == login_deny){
     if(!quiet_flag){fprintf(stderr,"failed: %s: Not able to log into RackSwitch\n",name);}
     exit(DID_FAILURE);
   }
   else{
     if(verbose_flag){printf("%s: Successfully logged into RackSwitch\n",name);}
   }
  }

 /********************************************
  ***
  ***	Send Configuration Request Message
  ***
  *******************************************/
 
 writebuf[0] = configuration_request;
 writebuf[1] = config_general;
 if(write(sock,writebuf,2*(sizeof(char))) < 0) {
   fprintf(stderr,"failed to write to socket\n");
   exit(DID_FAILURE);
 }

 /********************************************
   ***
   ***	Read General Configuration Message
   ***
   *******************************************/

 if(wait_frame(config_reply)){
   n = read(sock,readbuf,1);
   if(readbuf[0] == config_general){

     /* Configuration Status, one byte */
     n = read(sock,readbuf,1);
     
     /* Switch description, string */
     read_more = 1;
     while(read_more){         
       n = read(sock,readbuf,1);
       if(readbuf[0] == '\0'){
	 read_more = 0;
       }
       else{
	 read_more = 1;
       }
     }
     
     /* Serial number, string */
     read_more = 1;
     while(read_more){
       n = read(sock,readbuf,1);
       if(readbuf[0] == '\0'){
	 read_more = 0;
       }
       else{
	 read_more = 1;
       }
     }

       	  /* Version number, string */
     read_more = 1;
     while(read_more){
       n = read(sock,readbuf,1);
       if(readbuf[0] == '\0'){
	 read_more = 0;
       }
       else{
	 read_more = 1;
       }
     }

     /* number of configured temps, 1 byte */
     number_of_temp = 0; 
     n = read(sock,readbuf,1);
     number_of_temp = (int)readbuf[0];
     
     for(i=0;i<number_of_temp;i++){

       /* Temprature description, string */
       read_more = 1;
       while(read_more){ 
	 read_more = 1;
	 while(read_more){         
	   n = read(sock,readbuf,1);
	   if(readbuf[0] == '\0'){
	     read_more = 0;
	   }
	   else{
	     read_more = 1;
	   }
	 }
       }
       
       n = read(sock,readbuf,1); /* Temprature input ID */
       n = read(sock,readbuf,1); /* Tempratue unit */
       n = read(sock,readbuf,8); /* Temprature HI alarm */
       n = read(sock,readbuf,8); /* Temprature LO alarm */
       n = read(sock,readbuf,1); /* Temprature HI Alarm */
       n = read(sock,readbuf,1); /* Temprature LO Alarm */
       n = read(sock,readbuf,1); /* Temprature Alarm email */
     }
     /* Number of configured motherboards */
     number_of_config_mobo = 0;
     for(i=4;i>0;i--){
       read_more = 1;	
       while(read_more){
	 n=read(sock,readbuf,1);
	 if(n == 1){
	   read_more = 0;
	   number_of_config_mobo = number_of_config_mobo + (int)(readbuf[0]<<(8*(i-1)));
	 }
       }
     }
     /*
      * make sure the motherboard we are asked to turn of is configured
      */
     if(pnumb > number_of_config_mobo){
       if(!quiet_flag){
	 fprintf(stderr,"failed: %s asked to reboot port %d, but there are only %d ports configured\n",name,pnumb,number_of_config_mobo);
	 exit(DID_FAILURE);
       }
     }
     n = read(sock,readbuf,1); /* email alarms */
     n = read(sock,readbuf,4); /* email alarm delay */

     /* email addresses, string */
     read_more = 1;
     while(read_more){         
       n = read(sock,readbuf,1);
       if(readbuf[0] == '\0'){
	 read_more = 0;
       }
       else{
	 read_more = 1;
       }
     }
     
     n = read(sock,readbuf,4); /* reset action duration */
     n = read(sock,readbuf,4); /* power off action duration */
     n = read(sock,readbuf,4); /* power on action duration */
   }
   else{
     if(debug_flag){fprintf(stderr,"failed: %s: Did not receive general configuration frame when requested\n",name);}
     exit(DID_FAILURE);
   }
 }
 else{
   if(debug_flag){fprintf(stderr,"failed: %s: Did not receive configuration frame when requested\n",name);}
   exit(DID_FAILURE);
 }



 /******************************************
  ***
  ***	Send Action packet to switch
  ***	Off/On port <portnum>
  ***
  *****************************************/
 memset(writebuf,0,sizeof(writebuf));
 writebuf[0] = op_action; 
 writebuf[1] = (char)(number_of_config_mobo >> 24);
 writebuf[2] = (char)(number_of_config_mobo >> 16);
 writebuf[3] = (char)(number_of_config_mobo >> 8);
 writebuf[4] = (char)(number_of_config_mobo);
 
 writebuf[(pnumb*5)+0] = (char)(pnumb >> 24);
 writebuf[(pnumb*5)+1] = (char)(pnumb >> 16);
 writebuf[(pnumb*5)+2] = (char)(pnumb >> 8);
 writebuf[(pnumb*5)+3] = (char)(pnumb);
 writebuf[(pnumb*5)+4] = action_offon;

 if(write(sock,writebuf,(pnumb*5)+5) < 0) {
   fprintf(stderr,"failed to write to socket\n");
   exit(DID_FAILURE);
 }
 if(verbose_flag){
   printf("%s: sending action frame to switch:\n",name);
   for(i=0;i<(pnumb*5)+5;i++) 
     printf("0x%.2x ",writebuf[i]);
   printf("\n");
 }

  /******************************************
   ***
   ***	Send Configuration Request packet to switch
   ***
   *****************************************/
 memset(writebuf,0,sizeof(writebuf));
 writebuf[0] = configuration_request;
 writebuf[1] = boardnum;
 
 if(write(sock,writebuf,2*(sizeof(char))) < 0) {
   fprintf(stderr,"failed to write to socket\n");
   exit(DID_FAILURE);
 }
 if(verbose_flag){
   printf("%s: sending Request Configuration Frame from switch:\n",name);
   printf("0x%.2x 0x%.2x\n",writebuf[0],writebuf[1]);
 }

 /*******************************************
  ***
  ***	Read Switch Status Message
  ***
  ******************************************/
 while(success_off == 0){
   if(debug_flag){
     printf("%s: Status does not indicate port %d being rebooted. Looking again\n",name,pnumb);}
   if(wait_frame(message_status)){
     n = read(sock,readbuf,1); /* Rackswitch status */
     
     read_more = 1;
     while(read_more){         /* Date & time */
       n = read(sock,readbuf,1);
       if(readbuf[0] == '\0'){
	 read_more = 0;
       }
       else{
	 read_more = 1;
       }
     }
     number_of_temp = 0;
     n = read(sock,readbuf,1);
     number_of_temp = readbuf[0];
     for(i=0;i<number_of_temp;i++){
       read_more = 1;
       while(read_more){
	 n = read(sock,readbuf,1); /* Temprature input ID */
	 n = read(sock,readbuf,8); /* Temprature Value, Fahrenheit */
	 n = read(sock,readbuf,8); /* Temprature Vaule, Celcius */
	 n = read(sock,readbuf,1); /* Temprature Alarm */
       }
     }
     
     /* number of motherboards, 4 byte */
     number_of_section_config_mobo = 0;
     for(i=4;i>0;i--){
       read_more = 1;	
       while(read_more){
	 n=read(sock,readbuf,1);
	 if(n == 1){
	   read_more = 0;
	   number_of_section_config_mobo = number_of_section_config_mobo + (int)(readbuf[0]<<(8*(i-1)));
	 }
       }
     }
     
     for(i=0;i<number_of_section_config_mobo;i++){
       
       this_mobo = 0;
       for(j=4;j>0;j--){
	 read_more = 1;	
	 while(read_more){
	   n=read(sock,readbuf,1);
	   if(n == 1){
	     read_more = 0;
	     this_mobo = this_mobo + (int)(readbuf[0]<<(8*(j-1)));
	   }
	 }
       }
       if(debug_flag){printf("%s: port %d is currently ",name,this_mobo);}
       n = read(sock,readbuf,1); /* Motherboard status */
       if(debug_flag){printf("0x%.2x\n",readbuf[0]);}
       if((pnumb == this_mobo) && ((readbuf[0] == 0x02)||(readbuf[0] == 0x03))){
	 success_off = 1;
	 if(verbose_flag){printf("%s: Status shows port %d being rebooted\n",name,this_mobo);}
       }
     } /* end number_of_section_mobo loop */
     if(!success_off){
       if(verbose_flag){printf("%s: Status shows port %d NOT being rebooted, asking for status again\n",name,pnumb);}
     }
   }
   else{
     if(debug_flag){fprintf(stderr,"%s: Did not receive Switch Status Message\n",name);}
     exit(DID_FAILURE);
   }
 }


 if(success_off){
	 if(!quiet_flag){	
   printf("success: %s: successfully told RackSwitch to reboot port %d\n",name,pnumb);
	 }   
   alarm(0);
   exit_status = DID_SUCCESS;
 }
 return(exit_status);
}
/* And that is it. There is no more.
 * Maybe there  should be more?
 * Or maybe not?
 * But there is no more
 */
