#include"HTTP.h"

//定义http响应的一些状态信息
const char *ok_200_title = "OK";
const char *error_400_title = "Bad Request";
const char *error_400_form = "Your request has bad syntax or is inherently impossible to staisfy.\n";
const char *error_403_title = "Forbidden";
const char *error_403_form = "You do not have permission to get file form this server.\n";
const char *error_404_title = "Not Found";
const char *error_404_form = "The requested file was not found on this server.\n";
const char *error_500_title = "Internal Error";
const char *error_500_form = "There was an unusual problem serving the request file.\n";

int http_conn::epollfd=-1;
int http_conn::user_count=0;

MySqlPool* http_conn::pool=nullptr;

map<string,string> users_table;

/**
 * 初始化用户列表,将用户信息读取到内存中,避免了每次访问都要进行一次mysql查询。
 */
void::http_conn::init_users(MySqlPool*pool){
    MYSQL*conn=nullptr;
    connectionRAII my_conn=connectionRAII(&conn,pool);//建立一条连接

    if(mysql_query(conn,"SELECT username,passwd FROM user")){
        Log::get_instance()->write_log(ERROR,"HTTP.cpp","init_users",29,"Error at function<mysql_query()>");
    }

    MYSQL_RES*result=mysql_store_result(conn);//保存该连接查询得到的结果

    int num_fields=mysql_num_fields(result);    //获取查询结果的列数
    
    MYSQL_FIELD *fields = mysql_fetch_fields(result);//返回所有字段结构的数组

    while(MYSQL_ROW row=mysql_fetch_row(result)){
        string temp1(row[0]);
        string temp2(row[1]);
        users_table[temp1]=temp2;
    }

}

/**
 * 将文件描述符fd设置为非阻塞模式
 */
int setnonblocking(int fd)
{
    int old_option = fcntl(fd, F_GETFL);
    int new_option = old_option | O_NONBLOCK;
    fcntl(fd, F_SETFL, new_option);
    return old_option;
}

/**
 * 从epoll中注销fd,不再监视fd上的任何I/O事件
 */
void removefd(int epollfd, int fd)
{
    epoll_ctl(epollfd, EPOLL_CTL_DEL, fd, 0);
}

/**
 * 重新注册fd上的ev事件,同时根据TRIGMode参数确定采用ET还是LT触发
 * EPOLLONESHOT保证了事件ev只能被监听一次,一次监听之后如果想继续监听,必须重新注册该事件。
 * 这样做可以保证同一时刻,最多只有一个线程操作fd。
 */
void modfd(int epollfd, int fd, int ev, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)
        event.events = ev | EPOLLET | EPOLLONESHOT | EPOLLRDHUP;    //边缘触发
    else
        event.events = ev | EPOLLONESHOT | EPOLLRDHUP;  //水平触发

    epoll_ctl(epollfd, EPOLL_CTL_MOD, fd, &event);
}

/**
 * 向epoll中注册fd的读就绪事件。
 * one_shot参数决定是否注册为EPOLLONESHOT
 * TRIGMode参数决定触发模式
 */
void addfd(int epollfd, int fd, bool one_shot, int TRIGMode)
{
    epoll_event event;
    event.data.fd = fd;

    if (1 == TRIGMode)  //如果采用ET模式
        event.events = EPOLLIN | EPOLLET | EPOLLRDHUP;
    else    //如果采用LT模式
        event.events = EPOLLIN | EPOLLRDHUP;

    if (one_shot)
        event.events |= EPOLLONESHOT;
    epoll_ctl(epollfd, EPOLL_CTL_ADD, fd, &event);
    setnonblocking(fd);
}

/**
 * 公共的初始化函数
 * 初始化的是连接参数
 */
void http_conn::init(int client_socketfd,const sockaddr_in& addr,int mode,const char*root)
{
    sockfd=client_socketfd;
    address=addr;

    trig_mode=mode;

    addfd(epollfd,sockfd,true,mode);//注册监听connfd上的读就绪事件和挂断事件,并设置为oneshot,
    user_count++;
    
    strcpy(root_path,root);
    // printf("%s\n",root_path);

    init();
}

/**
 * 私有的初始化函数
 * 初始化的是请求参数。
 * 私有init()初始化的参数对于同一连接的不同请求,可能不同。
 */
void http_conn::init()
{
    memset(read_buffer,'\0',READ_BUFFER_SIZE);
    memset(write_buffer,'\0',WRITE_BUFFER_SIZE);
    memset(file_path,'\0',FILE_NAME_LENGTH);
    start_of_this_line=0;
    checked_idx=0;
    read_idx=0;
    write_idx=0;

    method=GET;
    url=nullptr;
    version=nullptr;
    host=nullptr;
    content=nullptr;
    connection=nullptr;
    content_length=0;
    
    linger=false;
    current_state=CHECK_STATE_REQUESTLINE;
    file_address=nullptr;
    bytes_to_send=0;
    bytes_have_send=0;

    cgi=0;

    lock=Locker();
    mysql_conn=nullptr;
}

/**
 * 关闭一条连接
 */
void http_conn::close_fd(){
    if(sockfd!=-1){
        removefd(epollfd,sockfd);
        close(sockfd);
        sockfd=-1;
        user_count--;
    }
}

/**
 * 处理HTTP请求的方法
 */
void http_conn::process(){
    HTTP_CODE ret=process_read();//解析请求
    HTTP_CODE request_ret=UNKNOWN;
    if(ret==NO_REQUEST){    //说明读取到的请求不完整,需要继续读取 / 请求体出错。  重新注册epollin事件&oneshot
        Log::get_instance()->write_log(INFO,"HTTP.cpp","process",148,"client:%d Fucntion<process_read> processed incomplete request",sockfd);
        modfd(epollfd,sockfd,EPOLLIN,trig_mode);
        return;
    }

    if(ret==GET_REQUEST){
        request_ret=do_request();
        Log::get_instance()->write_log(INFO,"HTTP.cpp","process",157,"client:%d Fucntion<do_request> processed request",sockfd);
    }
    else{
        Log::get_instance()->write_log(INFO,"HTTP.cpp","process",148,"client:%d Fucntion<process_read> processed bad request",sockfd);
        return;
    }
    bool write_ret=process_write(ret,request_ret);//生成响应,把响应写到write_buffer上
    if(write_ret == false){
        Log::get_instance()->write_log(ERROR,"HTTP.cpp","process",159,"client:%d Error at <Process_write>",sockfd);
        close_fd();//关闭连接
        return;
    }
    Log::get_instance()->write_log(INFO,"HTTP.cpp","process",159,"client:%d Fucntion<process_write()> processed successfully",sockfd);
    modfd(epollfd,sockfd,EPOLLOUT,trig_mode);//读事件处理完之后,注册写事件

}

/**
 * 从连接中读取数据到read_buffer中
 */
bool http_conn::read(){
    //read_buffer满了,直接false
    if(read_idx>=READ_BUFFER_SIZE){
        return false;
    }

    //read_buffer没满,则开始读取
    int bytes_read=0;
    if(trig_mode==1){   //采用ET,边缘触发.同一个事件只能被epoll监听到一次。所以必须一次读完。
        while(true){
            bytes_read=recv(sockfd,read_buffer+read_idx,READ_BUFFER_SIZE-read_idx,0);
            if(bytes_read==-1){//读取出错
                if(errno==EAGAIN||errno==EWOULDBLOCK){//errno信息为这两种,那么fd只可能是非阻塞I/O,说明其余数据还没准备好==>读完了本次读就绪事件所有的数据
                    break;
                }
                return false;
            }
            else if (bytes_read == 0)
            {
                return false;
            }
            read_idx += bytes_read;
        }
    }
    else{//采用LT,同一个事件可以被监听多次,报告多次
        bytes_read=recv(sockfd,read_buffer+read_idx,READ_BUFFER_SIZE-read_idx,0);
        if(bytes_read==-1)//读取出错
        {
            return false;
        }
        else if (bytes_read == 0)//连接已经关闭
        {
            return false;
        }
        read_idx+=bytes_read;
    }
    return true;
}

/**
 * 将response从write_buffer中写入到sockfd
 * 写完后,重新注册读就绪
 */
bool http_conn::write()
{
    int temp=0;
    /**
     * 没有需要发送的,那么不必发送;或者是全部发送完毕了,就需要清理本次发送的内容。重新监听读事件
     */
    if(bytes_to_send==0){
        modfd(epollfd,sockfd,EPOLLIN,trig_mode);   //由于每个事件都设置了one_shot,所以需要重新注册
        init();
        Log::get_instance()->write_log(INFO,"HTTP.cpp","write",257,"client:%d No bytes need to be sent",sockfd);
        return true;
    }
    /**
     * 还有内容需要发送
     */
    while(1){
        temp=writev(sockfd,iv,iv_count);
        // printf("client:%d 一次writev\n",sockfd);
        if(temp==-1)//发送中出现错误
        {
            if(errno==EAGAIN)//缓冲区满了
            {
                modfd(epollfd,sockfd,EPOLLOUT,trig_mode);//已经写了一次,要重新注册写事件
                Log::get_instance()->write_log(INFO,"HTTP.cpp","write",270,"client:%d 缓冲区已满",sockfd);
                printf("client:%d 缓冲区满\n",sockfd);
                return true;
            }
            unmmap();//出现其他错误,那么这个HTTP请求的响应就无法发送回去了
            // modfd(epollfd,sockfd,EPOLLIN,trig_mode);
            Log::get_instance()->write_log(INFO,"HTTP.cpp","write",257,"client:%d Error at function <writev()>",sockfd);
            printf("client:%d writev()出错\n",sockfd);
            return false;//其他错误直接false
        }
        /**
         * 发送中没有出现错误,按下面的方式处理
         */
        bytes_have_send+=temp;  //更新发送的字节数
        bytes_to_send-=temp;    //更新待发送的字节数
        if(bytes_have_send>=iv[0].iov_len){//头部部分(write_buffer的内容)发送完毕了
            iv[0].iov_base=nullptr;
            iv[1].iov_base=file_address+(bytes_have_send-write_idx);
            iv[0].iov_len=0;
            iv[1].iov_len=bytes_to_send;
        }
        else//write_buffer中的内容还没发送完
        {
            iv[0].iov_base=write_buffer+bytes_have_send;
            iv[0].iov_len=iv[0].iov_len-bytes_have_send;
        }

        if(bytes_to_send==0)//全部发送完了
        {
            Log::get_instance()->write_log(INFO,"HTTP.cpp","write()",300,"client:%d reponse:%s",sockfd,write_buffer);
            unmmap();   //解映射
            close(fd);
            modfd(epollfd,sockfd,EPOLLIN,trig_mode);//可以接受新的请求了
            if(linger){
                init(); //连接如果要保持,就需要重新初始化buffer等请求参数
                return true;
            }
            else{
                return false;
            }
            
        }
        else//仍有未发送的,那么循环继续
        {
            
        }

    }

}

/**
 * process_read返回HTTP请求解析的结果,一共有三种:
 * GET_REQUEST,请求解析正确;
 * NO_REQUEST,请求不完整,需要继续读入;
 * BAD_REQUEST,请求中存在语法错误
 */
HTTP_CODE http_conn::process_read(){
    Log::get_instance()->write_log(INFO,"HTTP.cpp","eventloop",271,"\nclient:%d\nrequest:%s",sockfd,read_buffer);
    LINE_STATUS line_status=LINE_OK;
    char*line=nullptr;
    HTTP_CODE ret=GET_REQUEST;
    while(((current_state==CHECK_STATE_CONTENT)&&(line_status==LINE_OK))||((line_status=parse_line())==LINE_OK))
    {
        line=get_line();//获取本行文本
        start_of_this_line=checked_idx;//start_of_this_line指向下一个待解析的行
        switch(current_state){
            case CHECK_STATE_REQUESTLINE:{
                ret=parse_request_line(line);
                if(ret==BAD_REQUEST){//其他返回值:NO_REQUEST,说明请求行正确处理了,可以继续往后处理。
                    Log::get_instance()->write_log(ERROR,"HTTP.cpp","process_read",333,"client:%d Bad request at RequestLine",sockfd);
                    return BAD_REQUEST;
                }
                break;
            }
            case CHECK_STATE_HEADERS:{
                ret=parse_headers(line);
                if(ret==BAD_REQUEST){
                    Log::get_instance()->write_log(ERROR,"HTTP.cpp","process_read",341,"client:%d Bad request at Headers",sockfd);
                    return BAD_REQUEST;
                }
                if(ret==GET_REQUEST){
                    return GET_REQUEST;
                }
                break;
            }
            case CHECK_STATE_CONTENT:{
                Log::get_instance()->write_log(INFO,"HTTP.cpp","process_read",327,"client:%d parse content",sockfd);
                // printf("开始parse content\n");
                ret=parse_content(line);
                if(ret==GET_REQUEST){
                    return GET_REQUEST;
                }
                if(ret==NO_REQUEST){
                    line_status=LINE_OPEN;//结束循环
                    return NO_REQUEST;
                }
                break;
            }
            default:{
                return INTERNAL_ERROR;
            }
        }
    }
    if(line_status==LINE_OPEN)
        return NO_REQUEST;
    else if(line_status==LINE_BAD)
        return BAD_REQUEST;
}

/**
 * 找到一行,判断该行是否完整,同时更新checked_idx等值
 */
LINE_STATUS http_conn::parse_line(){
    /**
     * 理解一个点:HTTP请求中,换行符是\r\n两个字符,且\r和\n两个字符一定是一起出现的
     */
    for(;checked_idx<read_idx;++checked_idx){
        if(read_buffer[checked_idx]=='\r'){
            if(checked_idx+1==read_idx) // '\r'字符是最后读入内容的最后一个字符,没有完整读入==>LINE_OPEN
            {
                return LINE_OPEN;
            }
            if(read_buffer[checked_idx+1]=='\n'){
                read_buffer[checked_idx++]='\0';
                read_buffer[checked_idx++]='\0';
                return LINE_OK;
            }
            // '\r'后面接其他字符,都是LINE_BAD
            return LINE_BAD;
        }

        if(read_buffer[checked_idx]=='\n'){
            if(checked_idx>1&&read_buffer[checked_idx-1]=='\r'){
                read_buffer[checked_idx-1]='\0';
                read_buffer[checked_idx++]='\0';
                return LINE_OK;
            }
            //其他情况都是出错情况
            return LINE_BAD;
        }
        
    }
    //执行到这里,说明在循环过程中,没有出现\r和\n中的任意一个 ==> 读取的内容不完整
    return LINE_OPEN;
}

//解析请求行
HTTP_CODE http_conn::parse_request_line(char*line){
    /**
     * HTTP请求的request line由 method+空格+url+空格+HTTP协议版本号 组成
     * 但是,有些客户端或代理会用\t来分隔
     */
    url=strpbrk(line," \t");
    if(!url){
        return BAD_REQUEST;
    }
    *url='\0';
    url++;
    char*Method=line;
    if(strcasecmp(Method,"GET")==0){
        method=GET;
    }
    else if(strcasecmp(Method,"POST")==0){
        method=POST;
        cgi=1;
    }
    else if(strcasecmp(Method,"HEAD")==0){
        method=HEAD;
    }
    else if(strcasecmp(Method,"PUT")==0){
        method=PUT;
    }
    else if(strcasecmp(Method,"DELETE")==0){
        method=DELETE;
    }
    else if(strcasecmp(Method,"TRACE")==0){
        method=TRACE;
    }
    else if(strcasecmp(Method,"OPTIONS")==0){
        method=OPTIONS;
    }
    else if(strcasecmp(Method,"CONNECT")==0){
        method=CONNECT;
    }
    else if(strcasecmp(Method,"PATCH")==0){
        method=PATCH;
    }
    else{
        //使用了无法识别的METHOD
        Log::get_instance()->write_log(ERROR,"HTTP.cpp","parse_requestline",153,"client:%d Bad Request because of unknown method",sockfd);
        return BAD_REQUEST;
    }

    //下面处理url
    url+=strspn(url," \t");//有时候会用多个空格、制表符来分隔开HTTP请求行的各个部分,这里可以跳过开头的\t和空格

    version=strpbrk(url," \t");//找到version部分
    if(!version)
    {
        Log::get_instance()->write_log(ERROR,"HTTP.cpp","parse_requestline",153,"client:%d Bad Request because of NULL version",sockfd); 
        return BAD_REQUEST;
    }
    *version='\0';
    version++;
    if(strcasecmp(version,"HTTP/1.1")!=0){
        Log::get_instance()->write_log(ERROR,"HTTP.cpp","parse_requestline",153,"client:%d Bad Request because of wrong version",sockfd);
        return BAD_REQUEST;
    }

    //strncasecmp(str1,str2,n)比较前n个字符
    if(strncasecmp(url,"http://",7)==0){
        url+=7;
        // url=strchr(url,'/');    //url中包含http,说明可能是完整的url,完整的url为http://<host>/<path>?……   这里利用strchr找到path
    }
    else if(strncasecmp(url,"https://",8)==0){
        url+=8; //跳过协议
        
    }
    url=strchr(url,'/');//让url指向资源
    //经过上面的处理之后,在正确情况下,url一定是只包含path
    if(url==nullptr||url[0]!='/'){
        return BAD_REQUEST;
    }
    if(strlen(url)==1)  //url是单独的一个'/'
        strcat(url,"home");
    
    current_state=CHECK_STATE_HEADERS;
    return NO_REQUEST;  //表示没有出错,但是还没处理完
}

/**
 * 处理HTTP请求中的headers部分
 */
HTTP_CODE http_conn::parse_headers(char*line){
    //先判断这一行是不是headers结束之后的空行\r\n\r\n
    if(line[0]=='\0'){
        /**
         * 实际上,空行字符串是"\r\n",但是在parse_line过程中,把\r\n都改成了'\0',方便把各行分开,每行都是一个完整的字符串
         * 所以这里通过判断line[0]是否为'\0'来判断是否为空行
         */
        if(content_length!=0){
            current_state=CHECK_STATE_CONTENT;
            return NO_REQUEST;
        }
        return GET_REQUEST;
    }

    //首先分开header和value
    char*value=strpbrk(line,":");   //请求头和值之间用:分开,这个:是第一个:
    *value='\0';
    value++;
    value+=strspn(value," \t");  //去掉value字符串起始的空白符

    //下面开始取头部的值。HTTP请求中有大量的头部,我们这里只取在response中会使用到的
    if(strcasecmp(line,"Host")==0){
        host=value;
    }
    else if(strcasecmp(line,"Connection")==0){
        connection=value;
        // printf("connection:%s\n",connection);
        if(strcasecmp(connection,"keep-alive")==0){
            // printf("lingher:%d\n",linger);
            linger=true;
        }
        else{
            linger=false;
        }
    }
    else if(strcasecmp(line,"Content-Length")==0){
        content_length=atoi(value);
    }
    else{
        Log::get_instance()->write_log(INFO,"HTTP.cpp","parse_headers",500,"client:%d Unknown header:%s:%s",sockfd,line,value);
    }
    return NO_REQUEST;
}

/**
 * 解析内容。为了简单起见,并不会分析内容,只是检查是否完整读取到了read_buffer中
 */
HTTP_CODE http_conn::parse_content(char*line){
    if(read_idx>=checked_idx+content_length){
        line[content_length]='\0';
        content=line;
        return GET_REQUEST;
    }
    return NO_REQUEST;
}

/**
 * 执行HTTP请求的函数
 */
HTTP_CODE http_conn::do_request(){
    strcpy(file_path,root_path);//先把根目录复制到文件路径中
    int len=strlen(root_path);

    const char *p = strrchr(url, '/');  //此时的url是host+path
    if(cgi==1){//这是post方法,此时要登录验证或者注册验证

        //先从content中提取出用户名、密码
        char user[100]="";
        char pwd[100]="";
        int i=5;
        for(;content[i]!='&';++i){
            user[i-5]=content[i];
        }
        user[i-5]='\0';
        i+=10;
        int j=0;
        for(;content[i]!='\0';++i,++j){
            pwd[j]=content[i];
        }
        pwd[j]='\0';

        if(*(p+1)=='r'){//注册cgi
            //此时的url是 '/registercgi',注册功能,要检查是否重复
            char *sql_insert = (char *)malloc(sizeof(char) * 200);
            strcpy(sql_insert, "INSERT INTO user(username, passwd) VALUES(");
            strcat(sql_insert, "'");
            strcat(sql_insert, user);
            strcat(sql_insert, "', '");
            strcat(sql_insert, pwd);
            strcat(sql_insert, "')");

            if(users_table.find(user)==users_table.end()){//不重复,把数据添加到库中,并返回注册成功页面
                lock.lock();
                connectionRAII conn(&mysql_conn,pool); // 需要查询了,所以申请一条连接
                int res = mysql_query(mysql_conn, sql_insert);
                users_table.insert(std::pair<string,string>(user,pwd));
                lock.unlock();

                if(!res)//成功
                {
                    strcpy(file_path+len,"/log.html");
                }
                else
                {
                    strcpy(file_path+len,"/logerror.html");
                }
            }
        }
        else if(*(p+1)=='l')//登录验证,此时的urk为logcgi
        {
            if (users_table.find(user) != users_table.end() && users_table[user] == pwd)
                strcpy(file_path+len, "/welcome.html");
            else
                strcpy(file_path+len, "/logError.html");
        }
    }
    else//非登录注册的情况
    {
        if(*(p+1)=='r')//此时url为register
        {
            strcpy(file_path+len,"/register.html");
        }
        else if(*(p+1)=='l')
        {
            strcpy(file_path+len,"/log.html");
        }
        else if((*(p+1)=='h')||(*(p+1)=='\0'))
        {
            strcpy(file_path+len,"/judge.html");
        }
        else if(*(p+1)=='p')
        {
            strcpy(file_path+len,"/picture.html");
        }
        else if(*(p+1)=='v')
        {
            strcpy(file_path+len,"/video.html");
        }
        else if(*(p+1)=='f')
        {
            strcpy(file_path+len,"/fans.html");
        }
    }
    
    //打开文件,检查文件状态,然后进行映射
    fd=open(file_path,O_RDONLY);
    
    if(fd<0)
    {
        Log::get_instance()->write_log(ERROR,"HTTP.cpp","do_request",604,"Error at function<open()>");
    }

    if(stat(file_path,&file_stat)<0){   //获取资源文件的属性
        return NO_RESOURCE;     //请求资源不存在
    }
    if(!(file_stat.st_mode&S_IROTH)){   //是否可读
        return FORBIDDEN_REQUEST;   //请求资源没有读权限
    }
    if(S_ISDIR(file_stat.st_mode)){ //申请的文件为目录,则BAD_REQUEST
        return BAD_REQUEST;
    }
    file_address=(char*)mmap(nullptr,file_stat.st_size,PROT_READ,MAP_PRIVATE,fd,0);

    return FILE_REQUEST;    //请求资源可以正常访问
}

/**
 * 解映射
 */
void http_conn::unmmap(){
    if(!file_address){
        munmap(file_address,file_stat.st_size);
        file_address=nullptr;
    }
}

/**
 * 生成HTTP请求的response,写入到wirte_buffer中
 */
bool http_conn::process_write(HTTP_CODE ret,HTTP_CODE req_ret){
    //根据ret生成不同的响应
    switch (ret)
    {
        case INTERNAL_ERROR:{
            if(false==(add_status_line(500,error_500_title)&&add_headers(strlen(error_500_form))&&add_content(error_500_form))){
                return false;
            }
            break;
        }
        case BAD_REQUEST:{//请求中存在语法错误
            if(false==(add_status_line(400,error_400_title)&&add_headers(strlen(error_400_form))&&add_content(error_400_form))){
                return false;
            }
            break;
        }
        case GET_REQUEST:{//process_read()返回GET_REQUEST,那么就开始执行请求
            switch (req_ret)
            {
            case NO_RESOURCE:{
                if(false==(add_status_line(404,error_404_title)&&add_headers(strlen(error_404_form))&&add_content(error_404_form))){
                    Log::get_instance()->write_log(ERROR,"HTTP.cpp","process_write",701,"client:%d Error when adding status line 404",sockfd);
                    return false;
                }
                break;
            }
            case FORBIDDEN_REQUEST:{
                if(false==(add_status_line(403,error_403_title)&&add_headers(strlen(error_403_form))&&add_content(error_403_form))){
                    Log::get_instance()->write_log(ERROR,"HTTP.cpp","process_write",710,"client:%d Error when adding status line 403",sockfd);
                    return false;
                }
                break;
            }
            case FILE_REQUEST:{
                if(false==add_status_line(200,ok_200_title)){
                    Log::get_instance()->write_log(ERROR,"HTTP.cpp","process_write",715,"client:%d Error at function<Add_status_line> 200",sockfd);
                    return false;
                }
                if(file_stat.st_size!=0){
                    add_headers(file_stat.st_size);
                    //传输两个部分的数据,第一部分是头部,第二部分是文件
                    iv[0].iov_base=write_buffer;
                    iv[0].iov_len=write_idx;
                    iv[1].iov_base=file_address;
                    iv[1].iov_len=file_stat.st_size;
                    iv_count=2;
                    bytes_to_send=write_idx+file_stat.st_size;
                    return true;
                }
                else{
                    //文件为空时的处理
                    const char *ok_string = "<html><body></body></html>";
                    add_headers(strlen(ok_string));
                    if (!add_content(ok_string)){
                        Log::get_instance()->write_log(ERROR,"HTTP.cpp","process_write",727,"client:%d Target file %s is null",sockfd,file_path);
                        return false;
                    }
                        
                }
                break;
                
            }        
            default:{
                return false;
            }
                
            }
        }    
        default:
            return false;
    }
    //除了FILE_REQUEST之外,其余都只发送头部,这里设置
    iv[0].iov_base=write_buffer;
    iv[0].iov_len=write_idx;
    iv_count=1;
    bytes_to_send=write_idx;
    return true;
}

/**
 * 添加response标头
 */
bool http_conn::add_response(const char*format,...){
    
    if(write_idx>=WRITE_BUFFER_SIZE)//写缓冲区满了
    {
        return false;
    }
    va_list args;//定义可变参数列表
    va_start(args,format);//初始化

    //把参数构成的字符串写入write_buffer中
    int len=vsnprintf(write_buffer+write_idx,WRITE_BUFFER_SIZE-write_idx-1,format,args);//size指定了buffer的大小。函数返回成功写入的字节数;如果buffer大小不够,那么也会返回本应写入的字节数。
    if(len>=WRITE_BUFFER_SIZE-write_idx-1){
        return false;//因为buffer中没有足够空间
    }
    write_idx+=len;
    va_end(args);   //一个宏,清理可变参数列表，释放资源
    return true;

}

/**
 * 添加空行
 */
bool http_conn::add_blank_line(){
    return add_response("%s","\r\n");
}

/**
 * 添加content
 */
bool http_conn::add_content(const char*cont){
    return add_response("%s",cont);
}

/**
 * 添加response的头部:content-length、Connection、空行
 */
bool http_conn::add_headers(int content_len){
    bool result_1=add_content_length(content_len);
    bool result_2=add_linger();
    bool result_3=add_blank_line();
    return result_1&&result_2&&result_3;
}

/**
 * 添加Connection头部
 */
bool http_conn::add_linger(){
    return add_response("Connection: %s\r\n",(linger==true)?"keep-alive":"close");
}

/**
 * 添加content-length头部
 */
bool http_conn::add_content_length(int content_len){
    return add_response("Content-Length: %d\r\n",content_len);
}

/**
 * 添加content-type头部
 */
bool http_conn::add_content_type()
{
    return add_response("Content-Type:%s\r\n", "text/html");
}

/**
 * 添加状态行
 */
bool http_conn::add_status_line(int status,const char*title){
    return add_response("%s %d %s\r\n","HTTP/1.1",status,title);
}