#include <geometry_msgs/Quaternion.h>
#include <ros/ros.h>
#include <serial/serial.h>
#include <sensor_msgs/Imu.h>
#include <std_msgs/String.h>
#include <std_srvs/Empty.h>
#include <string>
#include <tf/transform_broadcaster.h>
#include <tf/transform_datatypes.h>

std::string string_to_hex(const std::string& input)
{
    static const char* const lut = "0123456789ABCDEF";
    size_t len = input.length();

    std::string output;
    output.reserve(2 * len);
    for (size_t i = 0; i < len; ++i)
    {
        const unsigned char c = input[i];
        output.push_back(lut[c >> 4]);
        output.push_back(lut[c & 15]);
    }
    return output;
}

std::string char_to_hex(char c){
   static const char* const lut = "0123456789ABCDEF";
   return ""+lut[c >> 4] + lut[c & 15]; 
}


bool zero_orientation_set = false;

bool set_zero_orientation(std_srvs::Empty::Request&,
                          std_srvs::Empty::Response&)
{
  ROS_INFO("Zero Orientation Set.");
  zero_orientation_set = false;
  return true;
}


int main(int argc, char** argv)
{
  serial::Serial ser;
  std::string port;
  std::string tf_parent_frame_id;
  std::string tf_frame_id;
  std::string imu_frame_id;
  double time_offset_in_seconds;

  tf::Quaternion orientation;
  tf::Quaternion zero_orientation;

  std::string partial_line = "";

  ros::init(argc, argv, "mpu6050_serial_to_imu_node");

  ros::NodeHandle private_node_handle("~");
  private_node_handle.param<std::string>("port", port, "/dev/ttyUSB0");
  private_node_handle.param<std::string>("tf_parent_frame_id", tf_parent_frame_id, "imu_base");
  private_node_handle.param<std::string>("tf_frame_id", tf_frame_id, "imu");
  private_node_handle.param<std::string>("imu_frame_id", imu_frame_id, "imu_base");
  private_node_handle.param<double>("time_offset_in_seconds", time_offset_in_seconds, 0.0);

  ros::NodeHandle nh;
  ros::Publisher imu_pub = nh.advertise<sensor_msgs::Imu>("imu", 50);
  ros::ServiceServer service = nh.advertiseService("set_zero_orientation", set_zero_orientation);

  ros::Rate r(1000); // 1000 hz

  uint8_t size=15;
  //char input[16];
  while(ros::ok())
  {
    try
    {
      if (ser.isOpen())
      {
        //ser.write("$MIA,I,B,115200,R,100,D,Y,Y*C4");
        // read string from serial device
        if(ser.available())
        {
          std::string input_0 = ser.read(size); 
          std::string input_hex = string_to_hex(input_0); 
          //ROS_ERROR_STREAM("input_str: " << input_hex);
          std::string input;
          std::string flag;
          std::string::size_type position;
          position = input_hex.find("AA00");
          if (position != input_hex.npos) {
              //ROS_ERROR_STREAM("input_str: " << position);
              if(position!=0){
                  std::string input_1 = ser.read((position/2)); 
                  input=input_0+input_1;
              }else{
                  input=input_0;
              }
       
              char *chr = &input[0u];
              short angle = ((chr[3] & 0xFF) | ((chr[4] << 8) & 0XFF00));
              //ROS_ERROR_STREAM("angle: " << (double)angle/100.0);
              tf::Quaternion orientation = tf::createQuaternionFromYaw((double)angle*3.14/(180*100));
              

              if (!zero_orientation_set)
              {
                  zero_orientation = orientation;
                  zero_orientation_set = true;
              }

              tf::Quaternion differential_rotation;
              differential_rotation = zero_orientation.inverse() * orientation;
              // calculate measurement time
              ros::Time measurement_time = ros::Time::now() + ros::Duration(time_offset_in_seconds);
              // publish imu message
              sensor_msgs::Imu imu;
              imu.header.stamp = measurement_time;
              imu.header.frame_id = imu_frame_id;
              quaternionTFToMsg(differential_rotation, imu.orientation);
              // i do not know the orientation covariance
              imu.orientation_covariance[0] = 1000000;
              imu.orientation_covariance[1] = 0;
              imu.orientation_covariance[2] = 0;
              imu.orientation_covariance[3] = 0;
              imu.orientation_covariance[4] = 1000000;
              imu.orientation_covariance[5] = 0;
              imu.orientation_covariance[6] = 0;
              imu.orientation_covariance[7] = 0;
              imu.orientation_covariance[8] = 0.000001;
              // angular velocity is not provided
              imu.angular_velocity_covariance[0] = -1;
              // linear acceleration is not provided
              imu.linear_acceleration_covariance[0] = -1;
              imu_pub.publish(imu);
              // publish tf transform
              static tf::TransformBroadcaster br;
              tf::Transform transform;
              transform.setRotation(differential_rotation);
              br.sendTransform(tf::StampedTransform(transform, measurement_time, tf_parent_frame_id, tf_frame_id));

          }else{
              ROS_ERROR_STREAM("NOT found " );
          } 

          //ROS_ERROR_STREAM("input: " << input.size());
          //ROS_ERROR_STREAM("input_str: " << string_to_hex(input));
        }
          
      }
      else
      {
        // try and open the serial port
        try
        {
          ser.setPort(port);
          ser.setBaudrate(115200);
          serial::Timeout to = serial::Timeout::simpleTimeout(1000);
          ser.setTimeout(to);
          ser.open();
        }
        catch (serial::IOException& e)
        {
          ROS_ERROR_STREAM("Unable to open serial port " << ser.getPort() << ". Trying again in 5 seconds.");
          ros::Duration(5).sleep();
        }

        if(ser.isOpen())
        {
          //ser.write("$MIA,I,B,115200,R,10,D,Y,Y*C4");
          ser.write("$MIB,RESET*87");
          ROS_DEBUG_STREAM("Serial port " << ser.getPort() << " initialized.");
        }
        else
        {
          //ROS_INFO_STREAM("Could not initialize serial port.");
        }

      }
    }
    catch (serial::IOException& e)
    {
      ROS_ERROR_STREAM("Error reading from the serial port " << ser.getPort() << ". Closing connection.");
      ser.close();
    }
    ros::spinOnce();
    r.sleep();
  }
}

