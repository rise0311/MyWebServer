#include"Log.h"

Locker Log::lock=Locker();//初始化静态变量锁

Log::Log(){
    number_of_line=0;
}
Log::~Log(){
    close(fd);
}
bool Log::init(const char*file,int buf_size,int split_lines,bool close)
{
    close_log=close;
    split_line_number=split_lines;
    buffer_size=buf_size;
    buffer=new char[buffer_size];
    number_of_line=0;
    memset(buffer,'\0',buffer_size);

    time_t currect_time=time(NULL);
    struct tm*sys_time=localtime(&currect_time);
    day=sys_time->tm_mday;

    const char*p=strrchr(file,'/');
    char log_full_name[256]="";

    /**
     * 通过传递的文件路径,构造log文件的名字,确定目录
     */
    if(p==nullptr)//p为nullptr,则file不带路径
    {
        snprintf(log_full_name, 255, "%d_%02d_%02d_%s", sys_time->tm_year + 1900, sys_time->tm_mon + 1, sys_time->tm_mday, file);
    }
    else//file带路径
    {
        strcpy(log_file,p+1);
        strncpy(log_dir,file,p-file+1);
        snprintf(log_full_name, 255, "%s%d_%02d_%02d_%s", log_dir, sys_time->tm_year + 1900, sys_time->tm_mon + 1, sys_time->tm_mday, log_file);
    }
    fd=open(log_full_name,O_CREAT|O_WRONLY|O_APPEND,0644);//打开文件
    if(fd<0){
        return false;
    }
    return true;
}

//这个函数还可以进一步提高性能。几个方向:一次写多条日志/多线程来写
bool Log::write_log(LOGLEVEL level,string file_name,string function_name,int line_number,string format,...){
    /**
     * @param level  日志登记
     * @param file_name checkpoint所在的文件
     * @param function_name checkpoint所在的函数
     * @param line_number checkpoint的行号
     * @param format 日志信息的格式 
     */

    /**
     * 获取时间
     */
    struct timeval current={0,0};
    gettimeofday(&current,NULL);    //获取当前时间,返回秒和微秒部分。秒是1970.1.1 UTC至今的时间 
    struct tm*sys_time=localtime(&current.tv_sec);  //将Unix时间戳转化为当前时区(系统设置的时区)的时间

    /**
     * 下面生成log的前缀部分
     */
    char prev[10]="";
    switch (level)
    {
    case ERROR:{
        strcpy(prev,"[error]");
        break;
    }
    case WARNING:{
        strcpy(prev,"[warning]");
        break;
    }
    case DEBUG:{
        strcpy(prev,"[debug]");
        break;
    }
    case INFO:{
        strcpy(prev,"[info]");
        break;
    }
    default:
        strcpy(prev,"[info]");
        break;
    }

    /**
     * 下面开始正式写入的过程,把log写入buffer
     * 先检查是否需要新的log文件
     * 然后构造要写的log
     * 最后把内容写入到log文件中
     */

    lock.lock(); //这一步加锁是否有必要呢?  有必要
    number_of_line++;
    if(day!=sys_time->tm_mday||number_of_line%split_line_number==0)//需要用新的日志文件的两种情况:不是同一天/当前日志文件行数超过限制
    {
        /**
         * 首先关闭当前的日志文件。注意在关闭之前,保证之前的写操作都同步到磁盘上
         */
        fsync(fd);  //先同步,保证该打开文件的缓冲流的数据全部写回到文件中
        close(fd);  //关闭日志文件

        char tail[16]="";
        snprintf(tail,16,"%d_%02d_%02d",sys_time->tm_year + 1900, sys_time->tm_mon + 1, sys_time->tm_mday);
        
        char new_log_name[256]="";
        if(day!=sys_time->tm_mday)//不同天
        {
            snprintf(new_log_name,255,"%s%s%s",log_dir,tail,log_file);//新的log文件名更新了时间
            number_of_line=0;
            day=sys_time->tm_mday;
        }
        if(number_of_line%split_line_number==0){
            snprintf(new_log_name,255,"%s%s%s.%d",log_dir,tail,log_file,number_of_line/split_line_number);
        }
        //打开新的日志文件
        fd=open(new_log_name,O_CREAT|O_APPEND|O_WRONLY,0644);
        if(fd<0) return false;
    }
    lock.unlock();

    //构造要写的log
    va_list valst;
    va_start(valst, format);

    lock.lock();

    int n = snprintf(buffer, 48, "%d-%02d-%02d %02d:%02d:%02d.%06ld %s ",
        sys_time->tm_year + 1900, sys_time->tm_mon + 1, sys_time->tm_mday,
        sys_time->tm_hour, sys_time->tm_min, sys_time->tm_sec, current.tv_usec, prev);//写时间、日志类型
    int k=sprintf(buffer+n,"file:%s,line:%d ",file_name.c_str(),line_number);
    int m = vsnprintf(buffer + n+k, buffer_size - n - 1, format.c_str(), valst);  //写具体的内容
    buffer[n + m + k] = '\n';
    buffer[n + m + k + 1] = '\0';

    //写入文件中
    write(fd,buffer,strlen(buffer));
    lock.unlock();

    va_end(valst);
    return true;

}