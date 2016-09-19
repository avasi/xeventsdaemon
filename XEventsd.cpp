#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <syslog.h>
#include <string>
#include <cstring>
#include <sys/inotify.h>
#include <vector>
#include <signal.h>
#include <iostream>
#include <ctype.h>

using namespace std;

#define DAEMON_NAME "XEventsd"
#define EVENT_SIZE (sizeof (struct inotify_event))
#define EVENT_BUFF_LEN (1024*(EVENT_SIZE + 16))
#define READ_BUFF_SIZE (1024)
#define MINION_SLEEP_TIME (3)


const char* watch_dir = NULL;
string xevents_dir;
const char* wrapper_path = "/usr/bin/XEventsWrapper.py";
bool debug = false;



bool log(char const *arg1,char const *arg2= NULL){
    
    if (arg2 != NULL){
        syslog(LOG_NOTICE, arg1,arg2);
    }
    else{
        syslog(LOG_NOTICE, "%s",arg1);     
    }

    return true;
}


bool is_butia_connected(){

    FILE *in;
    string result;

    char const *cmd;
    char buff [READ_BUFF_SIZE];

    // Get   
    cmd = "lsusb -d 04d8:000c > /dev/null 2>&1 && echo connected";

    if (!(in = popen(cmd, "r"))){
        debug && log("is_butia_connected: Failed to get usb info.");
        return false;
    }

    while ( fgets( buff , READ_BUFF_SIZE, in ) != NULL ){
        result += buff;
    }

    pclose(in);

    if (result.find("connected") != string::npos){

        debug && log("is_butia_connected: USB4Butia is connected.");
        return true;
    }
    else{
        debug && log("is_butia_connected: USB4Butia is not connected.");
        return false;
    } 

}


string get_last_modified_file(string dir_path){

    FILE *in;
    string result = "";

    string cmd;
    char buff [READ_BUFF_SIZE];

    // Get   
    cmd = "ls -ltr " + dir_path + " | grep ^- | tail -1 | awk '{ print $(NF) }'";

    if (!(in = popen(cmd.c_str(), "r"))){
        debug && log("get_last_modified_file: Error. Failed to get last modified file.");
        return result;
    }

    while ( fgets( buff , READ_BUFF_SIZE, in ) != NULL ){
        result += buff;
    }

    pclose(in);

    return result;

}


void signal_callback_handler(int signum){

    syslog (LOG_NOTICE, "Caught signal SIGPIPE %d\n",signum);
}


bool dir_exists(const char* file_name){

    struct stat st;
    return (stat(file_name, &st) == 0 && S_ISDIR(st.st_mode));
}


bool has_suffix(const string &str, const string &suffix){

    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}


bool stop_process(){

    FILE *in;
    string file, result, pid;
    vector<string> pids;

    char const *cmd;
    char buff [READ_BUFF_SIZE];

    // Get previous running pids   
    cmd = "ps -axo pid,command | grep XEventsWrapper.py | grep -v grep | grep -v sh | grep -oE \"[0-9]+\"";
    
    
    if (!(in = popen(cmd, "r"))){
        debug && log("stop_process: Error. Failed to get current running pids.");
        return false;
    }

    while ( fgets( buff , READ_BUFF_SIZE, in ) != NULL ){
        file = buff;
        pid = file.substr(0, file.size() - 1);
        result += " " + pid;
        pids.push_back(pid);
    }

    pclose(in);

    
    // If we have a running process
    if (pids.size() > 0){

        debug && log("pids: %s",result.c_str());

        // Kill the running process
        cmd = ("kill -9 " + result).c_str();
        
        if (!(in = popen(cmd, "w"))){
            return false;            
        }

        pclose(in);
        //debug && log("stop_process: Process are down");    
    }
    
    return true;
}


bool start_process(string file_name){

    pid_t pid;
    //cmd = "python " + string(watch_dir) + "/" + file_name;

    pid = fork();

    if (pid < 0)
        return false;

    if (pid == 0){

        //Child
        string cmd,arg,cmd2,cmd3;
        
        cmd = string(wrapper_path);
        arg = string(watch_dir) + "/" + file_name;

        execl("/usr/bin/python","python", cmd.c_str(), "-f", arg.c_str(), (char*)0);

        syslog(LOG_NOTICE, "execl() failure!\n\n");
        exit(EXIT_FAILURE);
    }
    else{
        return true;
    }

}


void process(int fd){

    int length, i = 0;
    char buffer[EVENT_BUFF_LEN];
    string file_name;
    bool process_file;

    // process the event
    length = read(fd, buffer, EVENT_BUFF_LEN);

    if (length < 0) {
        debug && log("process: Failed to read event");
    }

    while (i < length){
        struct inotify_event *event = (struct inotify_event *)&buffer[i];
        if (event->len){

            if (!(event->mask & IN_ISDIR)){

                if (event->mask & IN_CLOSE_WRITE){
                    debug && log("The file %s was opened or modified.", event->name);
                    file_name = event->name;
                }

                if (event->mask & IN_MOVED_TO){
                    debug && log("The file %s was moved or renamed.", event->name);
                    file_name = event->name;
                }
            }

            i += EVENT_SIZE + event->len;
        }
    }

    
    // If a file was modified or changed 
    if ((file_name != "")&&(has_suffix(file_name,".py"))){

        debug && log("Processing %s.",file_name.c_str());

        // Checks if watch_dir exist
        if (dir_exists(watch_dir)){

            // Close previous running process
            stop_process();

            // Launch the new/modified file if USB4butia is connected
            if (is_butia_connected())
                start_process(file_name);
                    
        }    
        else{
           debug && log("Error: Failed to execute. The directory %s does not exist.", watch_dir);
        }       
    }
}   


void showhelp(char* s){

    cout << "Usage:   " << s << " [-option] [argument]" << endl;
    cout << "option:  " << "-h  show help information" << endl;
    cout << "         " << "-w folder_to_watch" << endl;
    cout << "         " << "-v  show version infomation" << endl;
    cout << "example: " << s << " -d -w folder_to_watch" << endl;
}


void start_minion(){

    debug && log("Starting XEvents Minion.");

    bool connected = false;

    while (true){

        // If USB4butia is not connected
        if (!is_butia_connected()){
            //Chequear que este el wrapper corriendo para no llamar porque si
            connected = false;
            stop_process();
        }
        // If USB4butia is connected
        else if (!connected){

            string file_name = get_last_modified_file(watch_dir);
            connected = true;

            //debug && log(file_name.c_str());

            if (!file_name.empty()){
                start_process(file_name);
            }
            
        }
        sleep(MINION_SLEEP_TIME);
    }
}


int main(int argc, char *argv[]) {

    pid_t pid, sid;
    int fd, wd,c;
    uint32_t mask = IN_CREATE | IN_MOVED_TO | IN_CLOSE_WRITE;

    // Check parameters
    while((c = getopt(argc,argv,"dhvw:")) != -1) {
        if (c == 'w'){
            watch_dir = optarg;
        } 
        else if (c == 'd') {
            debug = true;
        }
        else if (c == 'h') {
            showhelp(argv[0]);
        }
        else if (c == 'v') {
            std::cout << "The current version is 1.0" << endl;
        }
        else {
            showhelp(argv[0]);
            exit(EXIT_FAILURE);
        }
    }


    debug && std::cout << "Debug enabled." << endl;

    if (watch_dir == NULL){

        xevents_dir = getenv("HOME") + std::string("/XEvents");
        watch_dir = xevents_dir.c_str();

        debug && std::cout << "Using default directory. " << watch_dir << endl;
    }

    //Fork off the parent process
    pid = fork();
    if (pid < 0) { 
        exit(EXIT_FAILURE); 
    }

    //If we got a good PID, then we can exit the parent Process
    if (pid > 0) { 
        exit(EXIT_SUCCESS); 
    }

    //Change the file mode mask
    umask(0);

    //Set our Logging Mask and open the Log
    setlogmask(LOG_UPTO(LOG_NOTICE));
    openlog(DAEMON_NAME, LOG_CONS | LOG_NDELAY | LOG_PERROR | LOG_PID, LOG_USER);

    //Create a new Signature Id for our child
    sid = setsid();
    if (sid < 0) { 
        //Log any failure
        exit(EXIT_FAILURE); 
    }

    //Change the current working directory
    //If we cant find the directory we exit with failure.
    if ((chdir("/")) < 0) {
        //Log any failure
        exit(EXIT_FAILURE); 
    }

    //Close Standard File Descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    //----------------
    //Main Process
    //----------------

    /* Starting the minion to check USB4butia */

    pid_t minion_pid;

    //Fork off the parent process
    minion_pid = fork();

    if (minion_pid == 0) {
        //This is the minion
        start_minion();
        
    }
    else{

        debug && log("Starting XEvents Daemon.");

        /* creating the INOTIFY instance */
        fd = inotify_init();

        /* checking for error */
        if (fd < 0){
            debug && log("Error: Failed to create INOTIFY instance.");
        }

        /* Checks if watch_dir exist */ 
        if (!dir_exists(watch_dir)){
            debug && log("Error: Failed to watch to %s. The directory does not exist.",watch_dir);
            exit(EXIT_FAILURE);
        } 

        /* adding watch_dir to the watch list */
        wd = inotify_add_watch(fd, watch_dir, mask);

        if (wd < 0){
            debug && log("Error: Failed to watch to %s. INOTIFY error.",watch_dir);
            exit(EXIT_FAILURE);
        }
        else{
            debug && log("Success: Watching: %s",watch_dir);   
        }

        /* Catch Signal Handler SIGPIPE */
        signal(SIGPIPE, signal_callback_handler);

        /* Main loop */
        while(true){

            //Run our Process 
            try{
                process(fd);
            }catch(...){
                
            }
            
        }


        /* removing watch_dir from the whatch list */
        inotify_rm_watch(fd, wd);

        /* closing the INOTIFY instance */
        close(fd);

        debug && log("XEvents Daemon terminated.");

        //Close the log
        closelog ();
    }
}