#include "workerthread.h"
#include "mqtt.h"
#include "misc.h"

using namespace std;
string MAC;
int setup()
{
	MAC = getMac();
    //string hostname = exec("hostname");
	if(macOK(MAC) != 5)
	{
		log(3, "mac 错误");
		return 1;
	}
    //"cmd/IotApp/fa:04:39:46:16:2b/+/+/+/#"
    //string CMD = "cmd/IotApp/";
    CCMD = CCMD + MAC +"/+/+/+/#";
    cout << "云端cmd topic：" << CCMD << endl;
    //cmd_resp/:ProductName/:DeviceName/:CommandName/:RequestID/:MessageID
    CRSP = CRSP + MAC +"/";
    //2. 建立处理云端数据线程cmd
    thread (cloudThread).detach();
    //3. 消耗处理state消息，发给云端
    thread (localStateThread).detach();
	//处理消耗cmd rsp消息，发给云端
    thread (localRspThread).detach();
    //检测config有没有被改动的线程
    thread (readConfigThread).detach();
    return 0;
}

//把云端的命令下发下去
int cloudThread()
{
    //数据pop出来
    cout<<"send cloud cmd thread\n";
    log(6,"send cloud cmd thread");
    string data, ldata;
    int ret = -1;
    while(1) 
    {      
        //无论本地是否已经连接，都pop出来命令
        if(!local_q.queue_.empty())
        {

            data = local_q.pop();
            //先解析一下，再发给本地梯控
            if(parseCloud(data))
            {
                ret = mqtt_send(mosq_l, LCMD, data.c_str());
                if(ret != 0)
                {
                    //如果本地没有连接，这里也会报错
                    log(4, "mqtt_send local error=%i\n", ret);
                }
            }
            else
            {
                log(4, "云端数据出错");
            }
        }
        else
        {
            std::this_thread::sleep_for(chrono::milliseconds(10)); 
        }            

    }
    return 1;
}

int REGISTERED = 0;
/*0：未呼梯 1：已经呼梯 2：已经自动开门，当已经开门，但是检测到楼层已经过了，就取消开门，要不然电梯在运行的时候开门键一直按着
*/
//发送电梯状态线程
int localStateThread()
{
	//数据pop出来
	srand(time(0));
    cout<<"send state msg thread\n";
    log(6,"send state msg thread");
    string data, topic;
    int ret = -1;
    //数据pop出来，转换成云端数据
    while(1) 
    {                       
        //if(connected_c == 1 && !cloud_state_q.queue_.empty())
        //发送给云端这里要判断是否有连接，要不然mqtt_send会一直报错
        if(connected_c == 1)
        {
            //data = cloud_state_q.pop();
            data = cloud_state;
            topic = "upload_data/IotApp/"+MAC+"/sample/"+randomstring(26);
            ret = mqtt_send(mosq_c, topic,data.c_str());
            //cout << topic << ": " << data << endl;
            if(ret != 0)
            {
                log(4, "mqtt_send cloud state error=%i\n", ret);
				connected_c = 0;
            }             
        }
        else
        {
            std::this_thread::sleep_for(chrono::milliseconds(10)); 
            //sleep(1);
        }    
        //这里去检测是否到了目的楼层，然后自动开门10s
        if(REGISTERED == 1)
        {
            autoOpen(cloud_state); 
        }
        //自动开门之后，不断去检测现在电梯状态
        //else if(2 == REGISTERED)
        //{
        //    cancelAuto(cloud_state);
        //}
        //定时300ms发送       
        std::this_thread::sleep_for(chrono::milliseconds(300)); 
        


    }
    return 0;
}

//发送电梯回复指令线程
int localRspThread()
{
	//数据pop出来
    cout<<"send rsp msg thread\n";
    log(6,"send rsp msg thread");
    std::string data, topic;
    int ret = -1;
    //数据pop出来，转换成云端数据
    while(1) 
    {                       
        if(connected_c == 1 && !cloud_rsp_q.queue_.empty())
        {
            //cmd_resp/:ProductName/:DeviceName/:CommandName/:RequestID/:MessageID
            topic = CRSP+randomstring(26)+"/"+randomstring(10);
            //cout << topic << endl;
            data = cloud_rsp_q.pop();
            ret = mqtt_send(mosq_c, topic,data.c_str());
            log(6, "%s : %s", topic.c_str(),data.c_str());
            if(ret != 0)
            {
                log(4, "mqtt_send cloud rsp error=%i\n", ret);
            }             
        }
        else
        {
            std::this_thread::sleep_for(chrono::milliseconds(10)); 
            //sleep(1);
        }            

    }
    return 0;
}

/*
{"ID":"", "requestID":"", "cmd":"call", "floorNum":""}
{"ID":"", "requestID":"", "cmd":"close", "duration":""}
{"ID":"", "requestID":"", "cmd":"open", "duration":""}
{"ID":"", "requestID":"", "cmd":"cancelclose"}
{"ID":"", "requestID":"", "cmd":"cancelopen"}
*/
//将cloud的数据变成本地的json数据
bool parseCloud(string data)
{
    //{"ID":"8888", "requestID":"6666", "cmd":"open", "duration":"undefined"}
    //防止出现这样的问题
    Json::Value mcu;
    Json::Reader reader;
    Json::Value value;
    int duration = 0;
    bool err = false;
    Json::Value rsp;
    if(!reader.parse(data, value))
    {  
        return false; 
    }
    if(value["cmd"].isNull())
    {
        return false;
    }
    //操作电梯指令
    string cmd = value["cmd"].asString();
    if(cmd.compare("call") == 0)
    {
        if(value["floorNum_r"].isNull())
        {
            return false;
        }
        //登记呼梯楼层，到达后自动开门10秒，做到config里
        string floor = value["floorNum_r"].asString();
        registerFloor(floor);
    }
    else if(cmd.compare("close") == 0 || cmd.compare("open") == 0)
    {
        if(value["duration"].isNull())
        {
            return false;
        }
        string duration = value["duration"].asString();
        if(!isNum(duration))
        {
            return false;
        }
    }
    else if(cmd.compare("cancelopen") == 0)
    {

    }
    else if(cmd.compare("cancelclose") == 0)
    {

    }
    else
    {
        return false;
    }
    return true;
}

string floor = "unknown";
string door = "unknown";
string regFloor = "unknown";


void autoOpen(string state)
{
    Json::Value mcu;
    Json::Reader reader;
    Json::Value value;
    bool err = false;
    if(reader.parse(state, value))
    {       
        /*
        {
           "ID" : "00101",
           "door" : "closed", / "opened"
           "floorNum" : "6",
           "floorNum_r" : "30",
           "state" : "stop",
           "timestamp" : "1583737040199e"
        }
        */
        string floor = value["floorNum_r"].asString();
        string door = value["door"].asString();
        string state = value["state"].asString();
        //楼层已经到达，并且已经呼梯了，并且门已经在打开的情况下，自动开门10s一次
        if(floor == regFloor && REGISTERED == 1 && door == "opened")
        {
            string temp = "{\"ID\":\"00000\", \"sender\":\"autoOpen\",\"requestID\":";
            //string data =  temp + '"' + randomstring(10) + '"' + ",\"timestamp\":" + '"' + getTimeStamp() + '"' + ",\"cmd\":\"open\", \"duration\":" + '"' + autoTime + '"}';
            string data =  temp + '"' + randomstring(10) + '"' + ",\"timestamp\":" + '"' + getTimeStamp() + '"' + ",\"cmd\":\"open\", \"duration\":" + '"' + autoTime + "\"}";
            //string data = '{"ID":"00000", "sender":"autoOpen","requestID":'+ '"' + randomstring(10) + '"' + ',"timestamp":' + '"' + getTimeStamp() + '"' + ',"cmd":"open", "duration":"10"}';
            cout << data << endl;
            int ret = mqtt_send(mosq_l, LCMD, data.c_str());
            if(ret != 0)
            {
                //如果本地没有连接，这里也会报错
                log(4, "mqtt_send local autoOpen error=%i\n", ret);
            }
            //这时候状态转
            REGISTERED = 0;
        }
    }
}

void cancelAuto(string state)
{
    Json::Value mcu;
    Json::Reader reader;
    Json::Value value;
    bool err = false;
    if(reader.parse(state, value))
    {       
        /*
        {
           "ID" : "00101",
           "door" : "closed", / "opened"
           "floorNum" : "6",
           "floorNum_r" : "30",
           "state" : "stop",
           "timestamp" : "1583737040199e"
        }
        */
        string floor = value["floorNum_r"].asString();
        string door = value["door"].asString();
        string state = value["state"].asString();
      
        //如果这时候已经按了开门键，但是楼层已经不是目的楼层了，就需要释放开门键
        if(2 == REGISTERED)
        {
            if(floor != regFloor || door == "closed" || state != "stop")
            {
                //释放开门键
                string temp = "{\"ID\":\"00000\", \"sender\":\"autoOpen\",\"requestID\":";
                string data =  temp + '"' + randomstring(10) + '"' + ",\"timestamp\":" + '"' + getTimeStamp() + '"'+",\"cmd\":\"cancelopen\"}";
                //string data = '{"ID":"00000", "sender":"autoOpen","requestID":'+ '"' + randomstring(10) + '"' + ',"timestamp":' + '"' + getTimeStamp() + '"' + ',"cmd":"open", "duration":"10"}';
                cout << data << endl;
                int ret = mqtt_send(mosq_l, LCMD, data.c_str());
                if(ret != 0)
                {
                    //如果本地没有连接，这里也会报错
                    log(4, "mqtt_send local autoOpen cancelopen error=%i\n", ret);
                }
                REGISTERED = 0;
            }           
        }
    }
}

void  registerFloor(string floor)
{
    cout << "register " << floor << endl;
    regFloor = floor;
    REGISTERED = 1;
}


//增加实时读取config
int readConfigThread(void)
{
    struct stat file_stat;
    int err;
    time_t   mtime_now, mtime_pre;
    cout << "readConfigThread beigin" << endl;
    //这里使用文件修改时间参数
    while(1)
    {
        err = stat("/home/tikong/production/config.ini", &file_stat);
        if(err != 0)
        {
            perror("stat");
            log(3,"stat error");   
            return 1;
        }
        mtime_now = file_stat.st_mtime;
        if(mtime_now != mtime_pre)
        {
            printf("file changed, reload\n");
            log(4, "file changed, reload\n");
            mtime_pre = mtime_now;
            readAuto();
        }
        std::this_thread::sleep_for(chrono::seconds(3));
    }
  return 0;
}



#if 0
int reportErr(Json::Value rsp)
{
    Json::StreamWriterBuilder builder;
    builder["indentation"] = ""; // If you want whitespace-less output
    //发送成功给云端
    string data = Json::writeString(builder, rsp)+'\n';
    ele_cloud_q.push(data);
    log(4, "%s", data.c_str());        
}
int parseEle(string pkt)
{
    Json::Value mcu;
    Json::Reader reader;
    Json::Value value;
    int duration = 0;
    bool err = false;
    Json::Value rsp;
    rsp["hostname"] = hostname;
    rsp["timestamp"] = getTimeStamp();
    rsp["ID"] = ID;
    if(reader.parse(pkt, value))
    {       
        //!=0 防止初始化时出错  floorNum 供自己内部使用，是真实隔磁板的数目，
        //但是也是从最底层-3开始，而不是从1开始
        /*
        {
           "ID" : "00101",
           "door" : "closed",
           "floorNum" : "6",
           "floorNum_r" : "30",
           "state" : "stop",
           "timestamp" : "1583737040199"
        }
        */
        //使用floorNum，因为只有真实隔磁板才会递增，而面板的楼层时不连续的，所以很难判断出错误
        //那low high需要填写面板上楼层，因为只有这样才不会小于 或者大于
        //2020-3-27 可以按照实际的填写，因为遮光板数量不可能大于面板上的楼层数的
        //这里需要避免不断的上报错误信息
        if(!value["floorNum"].isNull() && stoi(value["floorNum"].asString()) !=0)
        {
            //如果超出楼层范围
            //2020-4-3 屏蔽这里的楼层报错
            int floor = stoi(value["floorNum"].asString());

            if(ele["state"].first.compare("up") == 0)
            {
                //上升过程
                if(floor_last > floor)
                {
                    rsp["msg"] = "电梯在上行，楼层在减少";
                    if(errType != 3)
                    {
                        cout << "电梯在上行，楼层在减少\n";
                        reportErr(rsp);
                        errType = 3;
                        //err = true;
                    }
                }
                //floor_last != floor 这里解决的是如果从上行到停止的时间里不要报错
                //floor_last != -1 是因为-1 到1 会跳变
                if(floor_last+1 != floor && floor_last != floor && floor_last != -1)
                {
                    rsp["msg"] = "电梯在上行，楼层没有递增";
                    if(errType != 4)
                    {
                        cout <<  "电梯在上行，楼层没有递增\n";
                        reportErr(rsp);
                        errType = 4;
                    }
                }
            }
            if(ele["state"].first.compare("down") == 0)
            {
                //下降过程
                if(floor_last < floor)
                {
                    rsp["msg"] = "电梯在下行，楼层在增加";
                    //
                    if(errType != 5)
                    {
                        cout << "电梯在下行，楼层在增加\n";
                        errType = 5;
                        reportErr(rsp);
                    }
                }
                if(floor_last-1 != floor && floor_last != floor && floor_last != 1)
                {
                    rsp["msg"] = "电梯在下行，楼层没有递减";
                    if(errType != 6)
                    {
                        cout << "电梯在下行，楼层没有递减\n";
                        reportErr(rsp);
                        errType = 6;
                    }
                }
            }
            floor_last = floor;
        }
        if(!value["state"].isNull())
        {
            string state = value["state"].asString();
            string state_last = ele["state"].first;
/*
            if((state_last.compare("up") == 0 && state.compare("down") == 0) || (state_last.compare("down") == 0 && state.compare("up") == 0))
            {
                rsp["msg"] = "电梯上下行状态突变";
                cout << "电梯上下行状态突变\n";
                //这里需要添加发送到1楼去复位，这里需要设置为梯控内部楼层，
                //而不能是面板楼层，如果要发送1,就需要梯控程序里有解析floorNum的分支
                //现在已经没有了，需要把这个分支加上
                //2020-3-27 目前没有把这个功能加上
                string data = "{\"ID\":\"00000\", \"cmd\":\"call\", \"__sender\":\"eleMonitor\",\"requestID\":\"for reset ele\",\"floorNum_r\":\"1\"}";
                //int ret = mqtt_send(mosq_l, "/cti/ele/cmd",data.c_str());

                //if(ret != 0)
                //{
                //    log(0, "mqtt_send reset floor error=%i\n", ret);
                //}
                reportErr(rsp);
            }
*/
            if(state.compare("down") == 0)
            {
                ele["state"].first = state;
                if(state_last.compare("up") == 0)
                {
                    rsp["msg"] = "电梯上行中突然下行";
                    cout << "电梯上行中突然下行\n";
                    reportErr(rsp);
                }
                //开始计时
                if(ele["state"].second.is_stopped())
                {
                    ele["state"].second.start();
                }
            }
        
            //compare to 0 is because init state is up
            else if(state.compare("up") == 0 && value["floorNum"].asString().compare("0") != 0)
            {
                ele["state"].first = state;
                if(state_last.compare("down") == 0)
                {
                    rsp["msg"] = "电梯下行中突然上行";
                    cout << "电梯下行中突然上行\n";
                    reportErr(rsp);
                }
                //开始计时
                if(ele["state"].second.is_stopped())
                {
                    ele["state"].second.start();
                }
            }
            else if(state.compare("stop") == 0)
            {
                ele["state"].first = state;
                if(!ele["state"].second.is_stopped())
                {
                    ele["state"].second.stop();
                }
            }

        }
        if(!value["door"].isNull())
        {
            string door = value["door"].asString();
            if(ele["door"].first.compare(door) != 0)
            {
                //如果不相同
                ele["door"].first = door;
                //就停止计时
                if(!ele["door"].second.is_stopped())
                {
                    ele["door"].second.stop();
                }
            }
            else
            {
                //如果状态相同，且为开门就开始计时，关门多久没关系
                if(ele["door"].second.is_stopped() && door.compare("opened") == 0)
                {
                    ele["door"].second.start();
                }
            }

        }

    }
    else
    {
        log(4, "parse elevator state error");
        return 1;
    }
    return 0;
}
void checkEle()
{
    Json::Value rsp;
    Json::StreamWriterBuilder builder;
    builder["indentation"] = ""; // If you want whitespace-less output
    rsp["hostname"] = hostname;
    rsp["timestamp"] = getTimeStamp();
    rsp["ID"] = ID;
    if(std::stoi(ele["state"].second.format(0, "%w")) > STATETIMEOUT)
    {
        //2020-4-3：这里只根据一直上行或下行来判断是否在维护
        //这里一直报警没有问题
        if(ele["state"].first.compare("stop") != 0)
        {
            rsp["msg"] = "电梯可能在维护";
            log(3,"电梯可能在维护");
            //发送成功给云端
            string data = Json::writeString(builder, rsp)+'\n';
            ele_cloud_q.push(data);
            //停止计时
            if(!ele["state"].second.is_stopped())
            {
                ele["state"].second.stop();
            }
        }
    }
    if(std::stoi(ele["door"].second.format(0, "%w")) > DOORTIMEOUT)
    {
        //这里没有问题，因为在报一次错误之后，会重新计时
        if(ele["door"].first.compare("opened") == 0)
        {
            rsp["msg"] = "电梯门开门状态超时";
            log(3,"电梯开门状态超时");
            //发送成功给云端
            string data = Json::writeString(builder, rsp)+'\n';
            ele_cloud_q.push(data);
            //停止计时
            if(!ele["door"].second.is_stopped())
            {
                ele["door"].second.stop();
            }
        }
    }

    rsp["msg"] = "heartbeat";
    //发送成功给云端
    string data = Json::writeString(builder, rsp)+'\n';
    ele_cloud_q.push(data);

}
#endif
