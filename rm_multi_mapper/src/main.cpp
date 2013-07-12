#include <ros/ros.h>
#include <actionlib/client/simple_action_client.h>
#include <actionlib/client/terminal_state.h>
#include <turtlebot_actions/TurtlebotMoveAction.h>
#include <rm_capture_server/Capture.h>
#include <pcl/visualization/pcl_visualizer.h>
#include <std_msgs/Float32.h>
#include <eigen_conversions/eigen_msg.h>
#include <nav_msgs/OccupancyGrid.h>
#include <octomap/OcTree.h>

#include <move_base_msgs/MoveBaseAction.h>
#include <octomap_msgs/BoundingBoxQuery.h>

#include <tf/transform_broadcaster.h>

#include <util.h>

void publish_transforms() {
	tf::TransformBroadcaster br;

	while (true) {
		tf::Transform transform;
		transform.setIdentity();

		br.sendTransform(
				tf::StampedTransform(transform, ros::Time::now(), "/map",
						"/cloudbot1/odom_combined"));

		usleep(33000);

	}

}

int main(int argc, char **argv) {
	ros::init(argc, argv, "multi_mapper");

	ros::NodeHandle nh;

	boost::shared_ptr<keypoint_map> map;

	boost::thread t(publish_transforms);

	// create the action client
	// true causes the client to spin its own thread
	actionlib::SimpleActionClient<move_base_msgs::MoveBaseAction> ac(
			"/cloudbot1/move_base", true);

	ros::ServiceClient client = nh.serviceClient<rm_capture_server::Capture>(
			"/cloudbot1/capture");

	ros::Publisher servo_pub = nh.advertise<std_msgs::Float32>(
			"/cloudbot1/mobile_base/commands/servo_angle", 1);

	ROS_INFO("Waiting for action server to start.");
	// wait for the action server to start
	ac.waitForServer(); //will wait for infinite time

	ROS_INFO("Action server started, sending goal.");
	// send a goal to the action

	for (int j = 0; j < 3; j++) {
		for (int i = 0; i < 12; i++) {
			move_base_msgs::MoveBaseGoal goal;
			goal.target_pose.header.frame_id = "base_link";
			goal.target_pose.header.stamp = ros::Time::now();

			goal.target_pose.pose.position.x = 0;
			goal.target_pose.pose.position.y = 0;
			goal.target_pose.pose.position.z = 0;

			tf::Quaternion q;
			q.setRotation(tf::Vector3(0,0,1), M_PI/4);
			tf::quaternionTFToMsg(q, goal.target_pose.pose.orientation);

			ac.sendGoal(goal);

			//wait for the action to return
			bool finished_before_timeout = ac.waitForResult(
					ros::Duration(30.0));

			if (finished_before_timeout) {
				actionlib::SimpleClientGoalState state = ac.getState();
				ROS_INFO("Action finished: %s", state.toString().c_str());
			} else
				ROS_INFO("Action did not finish before the time out.");

			for (float angle = -M_PI / 4; angle <= M_PI / 4;
					angle += M_PI / 18) {

				std_msgs::Float32 angle_msg;
				angle_msg.data = angle;
				servo_pub.publish(angle_msg);
				sleep(1);

				rm_capture_server::Capture srv;
				srv.request.num_frames = 1;
				if (client.call(srv)) {

					cv::Mat rgb = cv::imdecode(srv.response.rgb_png_data,
							CV_LOAD_IMAGE_UNCHANGED);

					cv::Mat depth = cv::imdecode(srv.response.depth_png_data,
							CV_LOAD_IMAGE_UNCHANGED);

					Eigen::Affine3d transform_d;
					tf::transformMsgToEigen(srv.response.transform,
							transform_d);
					Eigen::Affine3f transform = transform_d.cast<float>();

					if (map.get()) {
						keypoint_map map1(rgb, depth, transform);
						map->merge_keypoint_map(map1);

					} else {
						map.reset(new keypoint_map(rgb, depth, transform));
						map->align_z_axis();
						map->save("map_aligned");
					}

					std::cerr << map->keypoints3d.size() << " "
							<< map->descriptors.rows << " "
							<< map->weights.size() << std::endl;

					//vis.removeAllPointClouds();
					//vis.addPointCloud<pcl::PointXYZ>(
					//		accumulated_keypoints3d.makeShared(), "keypoints");
					//vis.spinOnce(2);

					//cv::imshow("Image", rgb);
					//cv::waitKey(2);

				} else {
					ROS_ERROR("Failed to call service /cloudbot1/capture");
					return 1;
				}

			}
		}

		//turtlebot_actions::TurtlebotMoveGoal goal;
		//goal.forward_distance = 0.5;
		//goal.turn_distance = 0;
		//ac.sendGoal(goal);

		//wait for the action to return
		bool finished_before_timeout = ac.waitForResult(ros::Duration(30.0));

		if (finished_before_timeout) {
			actionlib::SimpleClientGoalState state = ac.getState();
			ROS_INFO("Action finished: %s", state.toString().c_str());
		} else
			ROS_INFO("Action did not finish before the time out.");

		map->save("map_" + boost::lexical_cast<std::string>(j) + "_all");
		map->remove_bad_points(1);
		map->save("map_" + boost::lexical_cast<std::string>(j));
		map->optimize();

		/*
		 octomap::OcTree tree(0.05);
		 map->get_octree(tree);

		 octomap::point3d max = tree.getBBXMax();
		 octomap::point3d min = tree.getBBXMin();

		 ROS_INFO("Bounding volume x: %f %f, y: %f %f, z: %f %f\n", min.x(),
		 max.x(), min.y(), max.y(), min.z(), max.z());


		 grid->info.height = 100;
		 grid->info.width = 100;
		 grid->info.map_load_time = ros::Time::now();
		 grid->info.resolution = 0.05f;

		 grid->info.origin;

		 grid->header.frame_id = "odom_combined";
		 grid->header.seq = 0;
		 grid->header.stamp = ros::Time::now();

		 grid->data.resize(grid->info.height*grid->info.width, 0);
		 */

	}

	pcl::visualization::PCLVisualizer vis;
	vis.removeAllPointClouds();
	vis.addPointCloud<pcl::PointXYZ>(map->keypoints3d.makeShared(),
			"keypoints");
	vis.spin();

	std::cerr << "Error " << map->compute_error() << " Mean error "
			<< map->compute_error() / map->observations.size() << std::endl;

	map->optimize();

	std::cerr << "Error " << map->compute_error() << " Mean error "
			<< map->compute_error() / map->observations.size() << std::endl;

	vis.removeAllPointClouds();
	vis.addPointCloud<pcl::PointXYZ>(map->keypoints3d.makeShared(),
			"keypoints");
	vis.spin();

//exit
	return 0;
}
