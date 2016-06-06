#include <ros/ros.h>
#include <nav_msgs/Odometry.h>
#include <nav_msgs/Path.h>
#include <visualization_msgs/Marker.h>
#include <std_msgs/String.h>
#include "acl_fsw/QuadGoal.h"
#include "geometry_msgs/PoseStamped.h"
#include "geometry_msgs/TwistStamped.h"
#include "tf/tf.h"
#include <mutex>
#include <thread>
#include <std_srvs/Empty.h>
#include "trajectory_selector.h"
#include <cmath>
#include <tf2_ros/transform_listener.h>
#include <tf2_ros/transform_broadcaster.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.h>
#include <sensor_msgs/PointCloud2.h>
#include <pcl/filters/voxel_grid.h>
#include <pcl_conversions/pcl_conversions.h>
#include <pcl/point_cloud.h>
#include <pcl/point_types.h>
#include <time.h>
#include <stdlib.h>
#include "attitude_generator.h"

  
std::shared_ptr<tf2_ros::TransformListener> tf_listener_;
tf2_ros::Buffer tf_buffer_;


class TrajectorySelectorNode {
public:

	TrajectorySelectorNode(ros::NodeHandle & nh, std::string const& waypoint_topic, std::string const& pose_topic, std::string const& velocity_topic, std::string const& local_goal_topic, std::string const& samples_topic) {
		//nh.getParam("max_waypoints", max_waypoints);

		pose_sub = nh.subscribe(pose_topic, 1, &TrajectorySelectorNode::OnPose, this);
		velocity_sub = nh.subscribe(velocity_topic, 1, &TrajectorySelectorNode::OnVelocity, this);
		waypoints_sub = nh.subscribe(waypoint_topic, 1, &TrajectorySelectorNode::OnWaypoints, this);
  	    point_cloud_sub = nh.subscribe("/flight/xtion_depth/points", 1, &TrajectorySelectorNode::OnPointCloud, this);

		local_goal_pub = nh.advertise<acl_fsw::QuadGoal> (local_goal_topic, 1);
		poly_samples_pub = nh.advertise<nav_msgs::Path>(samples_topic, 1);
		vis_pub = nh.advertise<visualization_msgs::Marker>( "visualization_marker", 0 );

		trajectory_selector.InitializeLibrary(final_time);
		createSamplingTimeVector();

		for (int i = 0; i < trajectory_selector.getNumTrajectories(); i++) {
			poly_samples_pubs.push_back(nh.advertise<nav_msgs::Path>(samples_topic+std::to_string(i), 1));
		}

		tf_listener_ = std::make_shared<tf2_ros::TransformListener>(tf_buffer_);

		srand ( time(NULL) ); //initialize the random seed
		ROS_INFO("Finished constructing the trajectory selector node, waiting for waypoints");
	}

	void drawTrajectoryDebug() {
		size_t trajectory_index = 0;
		Eigen::Matrix<Scalar, Eigen::Dynamic, 3> sample_points_xyz_over_time =  trajectory_selector.sampleTrajectoryForDrawing(trajectory_index, sampling_time_vector, num_samples);

		nav_msgs::Path poly_samples_msg;
		poly_samples_msg.header.frame_id = "ortho_body";
		poly_samples_msg.header.stamp = ros::Time::now();
		mutex.lock();
		Vector3 sigma;
		for (size_t sample = 0; sample < num_samples; sample++) {
			poly_samples_msg.poses.push_back(PoseFromVector3(sample_points_xyz_over_time.row(sample), "ortho_body"));
			sigma = trajectory_selector.getSigmaAtTime(sampling_time_vector(sample));
		 	//drawGaussianPropagationDebug(sample, sample_points_xyz_over_time.row(sample), sigma);
		}
		mutex.unlock();
		poly_samples_pub.publish(poly_samples_msg);
	}

	void drawGaussianPropagationDebug(int id, Vector3 position, Vector3 sigma) {
		visualization_msgs::Marker marker;
		marker.header.frame_id = "ortho_body";
		marker.header.stamp = ros::Time();
		marker.ns = "my_namespace";
		marker.id = id;
		marker.type = visualization_msgs::Marker::SPHERE;
		marker.action = visualization_msgs::Marker::ADD;
		marker.pose.position.x = position(0);
		marker.pose.position.y = position(1);
		marker.pose.position.z = position(2);
		marker.scale.x = 0.1 + std::abs(position(0));
		marker.scale.y = 0.1 + std::abs(position(1));
		marker.scale.z = 0.1 + std::abs(position(2));
		marker.color.a = 0.15; // Don't forget to set the alpha!
		marker.color.r = 0.9;
		marker.color.g = 0.1;
		marker.color.b = 0.9;
		vis_pub.publish( marker );
	}

	void drawTrajectoriesDebug() {
		size_t num_trajectories = trajectory_selector.getNumTrajectories(); 
		
		for (size_t trajectory_index = 0; trajectory_index < num_trajectories; trajectory_index++) {

			Eigen::Matrix<Scalar, Eigen::Dynamic, 3> sample_points_xyz_over_time =  trajectory_selector.sampleTrajectoryForDrawing(trajectory_index, sampling_time_vector, num_samples);

			nav_msgs::Path poly_samples_msg;
			poly_samples_msg.header.frame_id = "ortho_body";
			poly_samples_msg.header.stamp = ros::Time::now();
			mutex.lock();
			Vector3 sigma;
			for (size_t sample = 0; sample < num_samples; sample++) {
				poly_samples_msg.poses.push_back(PoseFromVector3(sample_points_xyz_over_time.row(sample), "ortho_body"));
				sigma = trajectory_selector.getSigmaAtTime(sampling_time_vector(sample));
				if (trajectory_index == 0) {
					//drawGaussianPropagationDebug(sample, sample_points_xyz_over_time.row(sample), sigma);
				}
			}
			mutex.unlock();
			poly_samples_pubs.at(trajectory_index).publish(poly_samples_msg);
		}
	}

private:

	void createSamplingTimeVector() {
		num_samples = 10;
		sampling_time_vector.resize(num_samples, 1);

		double sampling_time = 0;
		double sampling_interval = (final_time - start_time) / num_samples;
		for (size_t sample_index = 0; sample_index < num_samples; sample_index++) {
    		sampling_time = start_time + sampling_interval*(sample_index+1);
    		sampling_time_vector(sample_index) = sampling_time;
    	}
  	}

	geometry_msgs::PoseStamped PoseFromVector3(Vector3 const& position, std::string const& frame) {
		geometry_msgs::PoseStamped pose;
		pose.pose.position.x = position(0);
		pose.pose.position.y = position(1);
		pose.pose.position.z = position(2);
		pose.header.frame_id = frame;
		pose.header.stamp = ros::Time::now();
		return pose;
	}

	void OnPose( geometry_msgs::PoseStamped const& pose ) {
		//ROS_INFO("GOT POSE");
		
		tf::Quaternion q(pose.pose.orientation.x, pose.pose.orientation.y, pose.pose.orientation.z, pose.pose.orientation.w);
		mutex.lock();
		tf::Matrix3x3(q).getRPY(roll, pitch, yaw);
		mutex.unlock();
		//std::cout << "Roll: " << roll << ", Pitch: " << pitch << ", Yaw: " << yaw << std::endl;

		static tf2_ros::TransformBroadcaster br;
  		geometry_msgs::TransformStamped transformStamped;
  
	    transformStamped.header.stamp = ros::Time::now();
	    transformStamped.header.frame_id = "body";
	    transformStamped.child_frame_id = "ortho_body";
	    transformStamped.transform.translation.x = 0.0;
	    transformStamped.transform.translation.y = 0.0;
	    transformStamped.transform.translation.z = 0.0;
	    tf2::Quaternion q_ortho;
	    q_ortho.setRPY(-roll, -pitch, 0);
	    transformStamped.transform.rotation.x = q_ortho.x();
	    transformStamped.transform.rotation.y = q_ortho.y();
	    transformStamped.transform.rotation.z = q_ortho.z();
	    transformStamped.transform.rotation.w = q_ortho.w();

	    // transformStamped.header.stamp = ros::Time::now();
	    // transformStamped.header.frame_id = "world";
	    // transformStamped.child_frame_id = "ortho_body";
	    // transformStamped.transform.translation.x = pose.pose.position.x;
	    // transformStamped.transform.translation.y = pose.pose.position.y;
	    // transformStamped.transform.translation.z = pose.pose.position.z;
	    // tf2::Quaternion q_ortho;
	    // q_ortho.setRPY(0, 0, tf::getYaw(q));
	    // transformStamped.transform.rotation.x = q_ortho.x();
	    // transformStamped.transform.rotation.y = q_ortho.y();
	    // transformStamped.transform.rotation.z = q_ortho.z();
	    // transformStamped.transform.rotation.w = q_ortho.w();

	    br.sendTransform(transformStamped);
	}

	void OnVelocity( geometry_msgs::TwistStamped const& twist) {
		//ROS_INFO("GOT VELOCITY");
		
		geometry_msgs::TransformStamped tf;
	    try {
	      // Need to remove leading "/" if it exists.
	      std::string twist_frame_id = twist.header.frame_id;
	      if (twist_frame_id[0] == '/') {
	        twist_frame_id = twist_frame_id.substr(1, twist_frame_id.size()-1);
	      }

	      tf = tf_buffer_.lookupTransform("body", twist_frame_id, 
	                                    ros::Time(twist.header.stamp),
	                                    ros::Duration(1.0/30));
	    } catch (tf2::TransformException &ex) {
	      ROS_ERROR("%s", ex.what());
	      return;
	    }

	    Eigen::Quaternion<Scalar> quat(tf.transform.rotation.w, tf.transform.rotation.x, tf.transform.rotation.y, tf.transform.rotation.z);
	    Matrix3 R = quat.toRotationMatrix();

		mutex.lock();
		trajectory_selector.setInitialVelocity(R*Vector3(twist.twist.linear.x, twist.twist.linear.y, twist.twist.linear.z));
		mutex.unlock();
	}

	Eigen::Vector3d VectorFromPose(geometry_msgs::PoseStamped const& pose) {
		return Vector3(pose.pose.position.x, pose.pose.position.y, pose.pose.position.z);
	}

	void OnWaypoints(nav_msgs::Path const& waypoints) {
		//ROS_INFO("GOT WAYPOINTS");
		int waypoints_to_check = std::min((int) waypoints.poses.size(), max_waypoints);

		waypoints_matrix.resize(4, waypoints_to_check);
		waypoints_matrix.col(0) << VectorFromPose(waypoints.poses[0]), 0.0;  // yaw is currently hard set to be 0
		double distance_so_far = 0.0;
		double distance_to_add;
		double distance_left;
		Eigen::Vector3d truncated_waypoint;
		Eigen::Vector3d p1, p2;
		int i;
		for (i = 0; i < waypoints_to_check - 1; i++){
			p1 = VectorFromPose(waypoints.poses[i]);
			p2 = VectorFromPose(waypoints.poses[i+1]);
			distance_to_add = (p2-p1).norm();
			if ((distance_to_add + distance_so_far) < carrot_distance) {
				distance_so_far += distance_to_add;
				waypoints_matrix.col(i + 1) << p2, 0.0; // yaw is currently hard set to be 0
			}
			else {
				distance_left = carrot_distance - distance_so_far;
				truncated_waypoint = p1 + (p2-p1) / distance_to_add * distance_left;
				distance_so_far = distance_so_far + distance_left;
				waypoints_matrix.col(i + 1) << truncated_waypoint, 0.0; // yaw is currently hard set to be 0
				i++;
				break;

			}
		}
		carrot_world_frame << waypoints_matrix(0, i), waypoints_matrix(1, i), waypoints_matrix(2, i); 
		


		geometry_msgs::TransformStamped tf;
	    try {
	      // Need to remove leading "/" if it exists.
	      std::string pose_frame_id = waypoints.poses[0].header.frame_id;
	      if (pose_frame_id[0] == '/') {
	        pose_frame_id = pose_frame_id.substr(1, pose_frame_id.size()-1);
	      }

	      tf = tf_buffer_.lookupTransform("ortho_body", pose_frame_id, 
	                                    ros::Time(waypoints.poses[0].header.stamp),
	                                    ros::Duration(1.0/30));
	    } catch (tf2::TransformException &ex) {
	      ROS_ERROR("%s", ex.what());
	      return;
	    }

	    geometry_msgs::PoseStamped pose_carrot_world_frame = PoseFromVector3(carrot_world_frame, "world");
	    geometry_msgs::PoseStamped pose_carrot_ortho_body_frame = PoseFromVector3(carrot_ortho_body_frame, "ortho_body");
	   
	    tf2::doTransform(pose_carrot_world_frame, pose_carrot_ortho_body_frame, tf);

	    carrot_ortho_body_frame = VectorFromPose(pose_carrot_ortho_body_frame);


	 //    visualization_msgs::Marker marker;
		// marker.header.frame_id = "ortho_body";
		// marker.header.stamp = ros::Time();
		// marker.ns = "my_namespace";
		// marker.id = 0;
		// marker.type = visualization_msgs::Marker::SPHERE;
		// marker.action = visualization_msgs::Marker::ADD;
		// marker.pose.position.x = carrot_ortho_body_frame(0);
		// marker.pose.position.y = carrot_ortho_body_frame(1);
		// marker.pose.position.z = carrot_ortho_body_frame(2);
		// marker.scale.x = 1;
		// marker.scale.y = 1;
		// marker.scale.z = 1;
		// marker.color.a = 0.5; // Don't forget to set the alpha!
		// marker.color.r = 0.9;
		// marker.color.g = 0.4;
		// marker.color.b = 0.0;
		// vis_pub.publish( marker );

	}



	void OnPointCloud(const sensor_msgs::PointCloud2ConstPtr& point_cloud_msg) {
		ROS_INFO("GOT POINT CLOUD");

		geometry_msgs::TransformStamped tf;
	    try {
	      tf = tf_buffer_.lookupTransform("ortho_body", "xtion_depth_optical_frame", 
	                                    ros::Time(),
	                                    ros::Duration(1.0/30));
	    } catch (tf2::TransformException &ex) {
	      ROS_ERROR("%s", ex.what());
	      return;
	    }

		pcl::PCLPointCloud2* cloud = new pcl::PCLPointCloud2; 
		pcl::PCLPointCloud2ConstPtr cloudPtr(cloud);
		
    	pcl_conversions::toPCL(*point_cloud_msg, *cloud);
    	pcl::PointCloud<pcl::PointXYZ>::Ptr xyz_cloud(new pcl::PointCloud<pcl::PointXYZ>);
    	pcl::fromPCLPointCloud2(*cloud,*xyz_cloud);

    	// edges are:
    	// 0,0
    	// 159, 0
    	// 0, 119
    	// 159,119
    	Vector3 blank;
    	blank << 0,0,0;

  		for (size_t i = 0; i < 100; i++) {
  			int x_rand_index = rand() % 160;
  			int y_rand_index = rand() % 120;
  			pcl::PointXYZ first_point = xyz_cloud->at(x_rand_index,y_rand_index);
  			geometry_msgs::PoseStamped point_cloud_xyz_body = PoseFromVector3(Vector3(first_point.x, first_point.y, first_point.z), "xtion_depth_optical_frame");
	    	geometry_msgs::PoseStamped point_cloud_xyz_ortho_body = PoseFromVector3(blank, "ortho_body");
	    	tf2::doTransform(point_cloud_xyz_body, point_cloud_xyz_ortho_body, tf);
  			point_cloud_xyz_samples_ortho_body.row(i) = VectorFromPose(point_cloud_xyz_ortho_body);
  		}

  		for (int i = 0; i < 100; i++) {

	  		if (isnan(point_cloud_xyz_samples_ortho_body(i,0))) {
	  			continue;
	  		}

  			visualization_msgs::Marker marker;
			marker.header.frame_id = "ortho_body";
			marker.header.stamp = ros::Time();
			marker.ns = "my_namespace";
			marker.id = 0;
			marker.type = visualization_msgs::Marker::SPHERE;
			marker.action = visualization_msgs::Marker::ADD;
			marker.pose.position.x = point_cloud_xyz_samples_ortho_body(0,0);
			marker.pose.position.y = point_cloud_xyz_samples_ortho_body(0,1);
			marker.pose.position.z = point_cloud_xyz_samples_ortho_body(0,2);
			//std::cout << "Trying to plot this point " << point_cloud_xyz_samples_ortho_body << std::endl;
			marker.scale.x = 0.3;
			marker.scale.y = 0.3;
			marker.scale.z = 0.3;
			marker.color.a = 0.5; // Don't forget to set the alpha!
			marker.color.r = 0.9;
			marker.color.g = 0.1;
			marker.color.b = 0.9;
			vis_pub.publish( marker );
			break;

  		}
  		

  		ReactToSampledPointCloud();
	
	}

	void ReactToSampledPointCloud() {
		Vector3 desired_acceleration = trajectory_selector.computeAccelerationDesiredFromBestTrajectory(point_cloud_xyz_samples_ortho_body, carrot_ortho_body_frame);
		
		//attitude_desired = attitude_generator.generateDesiredAttitude(desired_acceleration);
	}


	acl_fsw::QuadGoal local_goal_msg;

	ros::Subscriber waypoints_sub;
	ros::Subscriber pose_sub;
	ros::Subscriber velocity_sub;
	ros::Subscriber point_cloud_sub;

	ros::Publisher local_goal_pub;
	ros::Publisher poly_samples_pub;
	ros::Publisher vis_pub;

	std::vector<ros::Publisher> poly_samples_pubs;

	nav_msgs::Path waypoints;
	nav_msgs::Path previous_waypoints;
	int max_waypoints = 6;
	double carrot_distance = 5.0;

	double start_time = 0.0;
	double final_time = 0.5;

	Eigen::Vector4d pose_x_y_z_yaw;
	double roll, pitch, yaw;
	Eigen::Matrix<double, 4, Eigen::Dynamic> waypoints_matrix;

	Eigen::Matrix<Scalar, Eigen::Dynamic, 1> sampling_time_vector;
	size_t num_samples;

	Eigen::Matrix<Scalar, 100, 3> point_cloud_xyz_samples_ortho_body;

	std::mutex mutex;

	Vector3 carrot_world_frame;
	Vector3 carrot_ortho_body_frame;

	TrajectorySelector trajectory_selector;
	AttitudeGenerator attitude_generator;

public:
	EIGEN_MAKE_ALIGNED_OPERATOR_NEW
};


int main(int argc, char* argv[]) {
	std::cout << "Initializing trajectory_selector_node" << std::endl;

	ros::init(argc, argv, "TrajectorySelectorNode");
	ros::NodeHandle nh;

	TrajectorySelectorNode trajectory_selector_node(nh, "/waypoint_list", "/FLA_ACL02/pose", "/FLA_ACL02/vel", "/goal_passthrough", "/poly_samples");

	std::cout << "Got through to here" << std::endl;

	while (ros::ok()) {
		trajectory_selector_node.drawTrajectoriesDebug();
		ros::spinOnce();
	}
}