/*
 * centerinfo_serial_sender.cpp
 *
 *  Created on: Sep 27, 2016
 *      Author: aicrobo
 */


/*
 * curb_info_send.cpp
 *
 *  Created on: Sep 25, 2016
 *      Author: aicrobo
 */

#include<lane_center_keeping/center_line_msg.h>
#include<lane_center_keeping/obstacle_info_msg.h>

#include<stdio.h>
#include<stdlib.h>
#include<unistd.h>
#include<sys/types.h>
#include<sys/stat.h>
#include<fcntl.h>
#include<termios.h>
#include<errno.h>

#include<ros/ros.h>
#include<ros/console.h>
#include<time.h>

#include<ros/common.h>
#include<std_msgs/Bool.h>

struct timeval t;
#define BUFSIZE 10

char buffer[BUFSIZE];
char recv_buffer[BUFSIZE];
char object_buffer[5];
int dataLen=5;



/*
 * open_prot()- Open serial port1
 * Returns the file descriptor on success or -1 on error
 *
 * */

int speed_arr[]={B38400,B19200,B115200,B9600,B4800,B2400,B1200,B300};
int name_arr[]={38400,19200,115200,9600,4800,2400,1200,300};
double BAUD_SPEED;
int  SEND_FREQUENZCY;
std::string DEV_ID;

int fd;
double PID_KP ;//11.6*3.35=40
double PID_KI ;
double PID_KD ;


double KP;
double DRIFT_THRESHOLD;
static float steeling_previous=0;

int UsePID(float e)
{
  static int filberfirst=4;
  static int deltatime=0;
  static float ejian1=0,PID_e_sum=0;
  float ee1=0,d_out=0;
  if(filberfirst)
  {
    filberfirst--;
  }
  deltatime++;
  ee1=e-ejian1;
  if(ee1>10)
  {
    ee1=10;
  }
  else if(ee1<-10)
  {
    ee1=-10;
  }
  PID_e_sum+=e;
  if(PID_e_sum>200)
  {
    PID_e_sum=200;
  }
  else if(PID_e_sum<-200)
  {
    PID_e_sum=-200;
  }
  d_out=e*PID_KP;
  d_out+=ee1*PID_KD;
  if(std::abs(e)<3.3)
  {
    d_out+=PID_e_sum*(float)PID_KI;

  }
  ejian1=e;
  if(d_out>80)
  {
    d_out=80;
  }
  if(d_out<-60)
  {
    d_out=-60;
  }
  return -(int)(d_out+0.5f);
}

double getSteel(double intercept)
{

  if(std::abs(intercept)>=DRIFT_THRESHOLD)
  {
    double temp=KP*intercept;
    if(std::abs(temp)>45)
    {
      return 0;
    }
   else
    {
    return (KP*intercept);
    }
  }
  else
  {
    return 0;
  }


}

int open_port(const char*Dev)
{
  /*File descriptor for the port*/
  fd=open(Dev,O_RDWR|O_NOCTTY|O_NDELAY);
  if(fd==-1)
  {
    /*
     * Could not open the port
     * */
    perror("open_port:Unable to open /dev/ttyS0-");
    return -1;
  }
  else
  {
    fcntl(fd,F_SETFL,0);
    return fd;
  }
}

void set_speed(int speed)
{
  int i;
  int status;
  struct termios options;
  tcgetattr(fd,&options);

  options.c_lflag&=~(ICANON|ECHO|ECHOE|ISIG);
  options.c_oflag&=~OPOST;


  for(i=0;i<sizeof(speed_arr)/sizeof(int);i++)
  {
    if(speed==name_arr[i])
    {
      tcflush(fd,TCIOFLUSH);
      cfsetispeed(&options,speed_arr[i]);
      cfsetospeed(&options,speed_arr[i]);
      status=tcsetattr(fd,TCSANOW,&options);
      if(status!=0)
      {
        perror("tcsetattr fd");
        return;
      }
      tcflush(fd,TCIOFLUSH);
    }
  }
}

int BeStateExpanding(float e1,float e2,float e3)
{
  if(e2>0)
  {
	 if(e3>e2)
	 {
		 return 1;
	 }

  }
  else
  {
    if(e3<e2)
    {
      return 1;
    }

  }
  return 0;
}

int BestStateShrinking(float e1,float e2,float e3)
{
  if(e2>0)
  {
    if(e3<=e2)
    {
      return 1;
    }
  }
  else
  {
     if(e3>=e2)
     {
        return 1;
     }
  }
  return 0;
}

int BeStateKeeping(float e1,float e2,float e3)
{
  if(e1>-0.1f&&e1<0.1f)
  {
    if(e2>-0.2f&&e2<0.2f)
    {
      if(e3>-0.3f&&e3<0.3f)
      {
        if(std::abs(std::abs(e2)-std::abs(e3))<0.3)
        {
          return 1;
        }
      }
    }
  }
  return 0;
}

#define SWING 0
#define SHRINKING 1
#define KEEPING 2

int UsePID2(float e1,float e2,float e3)
{
  static int filberfirst=4;
  static int state=0;
  static int flag_shrinking=0;

  float d_out=0;
  float PID_kp_ex=30;
  float PID_kp_sh=30;
  float PID_kp_ke=30;

  if(filberfirst)
  {
    filberfirst--;
  }

  if(BeStateExpanding(e1,e2,e3))
  {
    state=SWING;
  }
  if(BeStateKeeping(e1,e2,e3))
  {
    state=SHRINKING;
  }
  if(BeStateKeeping(e1,e2,e3))
  {
    state=KEEPING;
  }
  printf("w1=%f,e2=%f,e3=%f\n",e1,e2,e3);
  printf("state=%d\n",state);
  switch(state)
  {
    case 0:
      d_out=e3;
      d_out-=e2;
      d_out*=std::abs(std::abs(e2)+std::abs(e3));
      d_out*=PID_kp_ex;
      d_out+=e2+e3;
      break;
    case 1:
      d_out=e3;
      d_out-=e2/2;
      d_out*=PID_kp_sh;
      d_out*=std::abs(std::abs(e2)+std::abs(e3));
      d_out+=(e2+e3);
      break;

    case 2:
      d_out=e3;
      d_out/=3;
      d_out*=PID_kp_ke;
      break;
    default:
      break;

  }
  if(d_out>30)
  {
    d_out=30;
  }
  if(d_out<-30)
  {
    d_out=-30;
  }
  printf("d_out=%f\n",-d_out);
  return -(int)(d_out+0.5f);

}

lane_center_keeping::center_line_msg::ConstPtr msg_(new lane_center_keeping::center_line_msg);
bool recv_flag;

lane_center_keeping::obstacle_info_msg::ConstPtr object_msg_(new lane_center_keeping::obstacle_info_msg());
bool object_recv_flag=false;

void send_CallBack(const lane_center_keeping::center_line_msg::ConstPtr &msg)
{

  msg_=msg;
  recv_flag=true;
}

void object_CallBack(const lane_center_keeping::obstacle_info_msg::ConstPtr&msg)
{
  object_msg_=msg;
}

int SerialcomRead(char*pbuffer,int dataLen)
{
  int ret;
  ret=read(fd,pbuffer,dataLen);
  return ret;
}


int ReadFromString(char ch)
{
 static int n=0;
 static char out=3;
 if(n==0)
 {
   if(ch==0x24)
   {
     n++;
   }
   else
   {
     n=0;
   }
   return 3;
 }
 if(n==1)
 {
   if(ch==0x30)
   {
     out=0;
     n++;
   }
   else if(ch==0x31)
   {
     out=1;
     n++;
   }
   else
   {
     n=0;
   }
   return 3;
 }
 if(n==2)
 {
   if(ch!=0x25)
   {
     out=2;
   }
   n=0;
 }
 return out;
}

int main(int argc,char**argv)
{
  ros::init(argc,argv,"center_info_sender");
  ros::NodeHandle hn;
  ros::param::get("~KP",KP);
  ros::param::get("~PID_KP",PID_KP);
  ros::param::get("~PID_KI",PID_KI);
  ros::param::get("~PID_KD",PID_KD);
  ros::param::get("~BAUD_SPEED",BAUD_SPEED);
  ros::param::get("~SEND_FREQUENZCY",SEND_FREQUENZCY);
  ros::param::get("~DRIFT_THRESHOLD",DRIFT_THRESHOLD);
  ros::param::get("~DEV_ID",DEV_ID);



  ros::Subscriber serial_sender=hn.subscribe("center_line_info",1,send_CallBack);

  ros::Subscriber object_msg=hn.subscribe("object",1,object_CallBack);
  ros::Publisher right_flag_pub=hn.advertise<std_msgs::Bool>("right_keeping_flag_msg",1);
  std_msgs::Bool flag;

  const char*dev=DEV_ID.c_str();
  fd=open_port(dev);

  set_speed(BAUD_SPEED);
  ros::Rate loop_rate(SEND_FREQUENZCY);
  /*
   * A count of how many messages we have sent.This is used to create a unique string for each message
   * */
  int count=0;
  memset(buffer,0,5);
  memset(object_buffer,0,5);
  while(ros::ok())
  {
/*
     if(recv_flag)
     {
       ROS_INFO("New msg recv_flag");
       recv_flag=false;
       int current_steel=UsePID2(msg_->intercept_0,msg_->intercept_1,msg_->intercept);
       std::cout<<"msg_.intercept0: "<<msg_->intercept_0<<"msg_.intercept0: "<<msg_->intercept_1<<"msg_.intercept0: "<<msg_->intercept<<std::endl;
       sprintf(buffer,"@%4d#\r\n",int(-current_steel));
       int nByte=write(fd,buffer,sizeof(buffer));
       if(nByte)
       {
         printf("发送成功！第%d次\n",count);
         ++count;
         ROS_INFO(buffer);
       }
     }
     */

     if(object_msg_->obstacle_property==1)
     {
       int n_Byte=write(fd,object_buffer,sizeof(object_buffer));
       if(n_Byte)
       {
         printf("发送成功！%s\n","1");
         printf(" %f %f %f #\r\n",object_msg_->position.x,object_msg_->position.y,object_msg_->position.z);
       }
     }

   else if(object_msg_->obstacle_property==0)
   {
     sprintf(object_buffer, "!%d%%", 0);
     int nByte = write(fd, object_buffer, sizeof(object_buffer));
     if (nByte)
     {
         printf("发送成功！%s\n","0");
     }
   }
else if(object_msg_->obstacle_property==2)
{
  sprintf(object_buffer, "!%d%%", 2);
     int nByte = write(fd, object_buffer, sizeof(object_buffer));
     if (nByte)
     {
         printf("发送成功！%s\n","2");
     }
}
else if(object_msg_->obstacle_property==3)
{
  sprintf(object_buffer, "!%d%%", 3);
     int nByte = write(fd, object_buffer, sizeof(object_buffer));
     if (nByte)
     {
         printf("发送成功！%s\n","2");
     }
}
     ros::spinOnce();
     loop_rate.sleep();
  }
  return 0;
}


/*
    int ret = read(fd, recv_buffer,dataLen);
    //std::string s(&recv_buffer[0],&recv_buffer[strlen(recv_buffer)]);
    if (ret)
    {
      //std::cout<<s<<std::endl;
      for (int i = 0; i < dataLen; i++)
      {
        int m = ReadFromString(recv_buffer[i]);
        if (m == 0)
        {
          flag.data = true;
          right_flag_pub.publish(flag);
          std::cout<<"0000000000000"<<std::endl;
        }
        else if (m == 1)
        {
          flag.data = false;
          right_flag_pub.publish(flag);
          std::cout<<"11111111111111111"<<std::endl;
        }
      }
    }
*/




