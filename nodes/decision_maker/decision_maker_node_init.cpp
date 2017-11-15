#include <ros/ros.h>
#include <ros/spinner.h>
#include <std_msgs/Float64.h>
#include <std_msgs/Float64MultiArray.h>
#include <std_msgs/Int32.h>
#include <std_msgs/String.h>
#include <stdio.h>
#include <tf/transform_listener.h>
#include <mutex>

// lib
#include <state.hpp>
#include <state_context.hpp>

#include <decision_maker_node.hpp>
//#include <vector_map/vector_map.h>

#include <autoware_msgs/lamp_cmd.h>
#include <autoware_msgs/lane.h>
#include <autoware_msgs/state.h>
#include <jsk_recognition_msgs/BoundingBoxArray.h>
#include <visualization_msgs/MarkerArray.h>
#include <random>

#include <geometry_msgs/Point.h>
#include <geometry_msgs/Pose.h>
#include <geometry_msgs/PoseStamped.h>

namespace decision_maker
{
#define TPNAME_BASED_LANE_WAYPOINTS_ARRAY "/based/lane_waypoints_array"
#define TPNAME_CONTROL_LANE_WAYPOINTS_ARRAY "/lane_waypoints_array"
#define LAMP_EMPTY  0
#define LAMP_LEFT   1
#define LAMP_RIGHT  2
#define LAMP_HAZARD 3
#define LAMP_ON 1
#define LAMP_OFF 0

void DecisionMakerNode::callbackStateSTR(int status)
{
  ROS_INFO("[%s]:%d\n", __func__, status);
  autoware_msgs::lamp_cmd lamp_msg;

  switch(status){
	  case LAMP_LEFT:
		  lamp_msg.l = LAMP_ON;
		  lamp_msg.r = LAMP_OFF;
		  break;
	  case LAMP_RIGHT:
		  lamp_msg.l = LAMP_OFF;
		  lamp_msg.r = LAMP_ON;
		  break;
	  case LAMP_HAZARD:
		  lamp_msg.l = LAMP_ON;
		  lamp_msg.r = LAMP_ON;
		  break;
	  case LAMP_EMPTY:
	  default:
		  lamp_msg.l = LAMP_OFF;
		  lamp_msg.r = LAMP_OFF;
		  break;
  }
}

void DecisionMakerNode::setupStateCallback(void)
{
  ctx->getStateObject(state_machine::DRIVE_STR_LEFT_STATE)
      ->setCallbackFunc(std::bind(&DecisionMakerNode::callbackStateSTR, this, 1));
  ctx->getStateObject(state_machine::DRIVE_STR_RIGHT_STATE)
      ->setCallbackFunc(std::bind(&DecisionMakerNode::callbackStateSTR, this, 2));
  ctx->getStateObject(state_machine::DRIVE_STR_STRAIGHT_STATE)
      ->setCallbackFunc(std::bind(&DecisionMakerNode::callbackStateSTR, this, 0));
}

void DecisionMakerNode::initROS(int argc, char **argv)
{
  // status subscriber
  Subs["sim_pose"] = nh_.subscribe("sim_pose", 20, &DecisionMakerNode::callbackFromSimPose, this);
  Subs["current_pose"] = nh_.subscribe("current_pose", 20, &DecisionMakerNode::callbackFromCurrentPose, this);
  Subs["current_velocity"] =
      nh_.subscribe("current_velocity", 20, &DecisionMakerNode::callbackFromCurrentVelocity, this);
  Subs["light_color"] = nh_.subscribe("light_color", 10, &DecisionMakerNode::callbackFromLightColor, this);
  Subs["light_color_managed"] =
      nh_.subscribe("light_color_managed", 10, &DecisionMakerNode::callbackFromLightColor, this);
  Subs["points_raw"] = nh_.subscribe("filtered_points", 1, &DecisionMakerNode::callbackFromPointsRaw, this);
  Subs["final_waypoints"] = nh_.subscribe("final_waypoints", 100, &DecisionMakerNode::callbackFromFinalWaypoint, this);
  Subs["twist_cmd"] = nh_.subscribe("twist_cmd", 10, &DecisionMakerNode::callbackFromTwistCmd, this);
  Subs["change_flag"] = nh_.subscribe("change_flag", 1, &DecisionMakerNode::callbackFromLaneChangeFlag, this);
  Subs["state_cmd"] = nh_.subscribe("state_cmd", 1, &DecisionMakerNode::callbackFromStateCmd, this);

  // vector map subscriber
  Subs["vector_map_area"] =
      nh_.subscribe("/vector_map_info/area", 1, &DecisionMakerNode::callbackFromVectorMapArea, this);
  Subs["vector_map_point"] =
      nh_.subscribe("/vector_map_info/point", 1, &DecisionMakerNode::callbackFromVectorMapPoint, this);
  Subs["vector_map_line"] =
      nh_.subscribe("/vector_map_info/line", 1, &DecisionMakerNode::callbackFromVectorMapLine, this);
  Subs["vector_map_crossroad"] =
      nh_.subscribe("/vector_map_info/cross_road", 1, &DecisionMakerNode::callbackFromVectorMapCrossRoad, this);

  // Config subscriber
  Subs["config/decision_maker"] =
      nh_.subscribe("/config/decision_maker", 3, &DecisionMakerNode::callbackFromConfig, this);

  // pub
  Pubs["state"] = nh_.advertise<std_msgs::String>("state", 1);
  Pubs["lane_waypoints_array"] = nh_.advertise<autoware_msgs::LaneArray>(TPNAME_CONTROL_LANE_WAYPOINTS_ARRAY, 10, true);
  Pubs["states"] = nh_.advertise<autoware_msgs::state>("/decisionmaker/states", 1, true);

  Pubs["lamp_cmd"] = nh_.advertise<autoware_msgs::lamp_cmd>("/lamp_cmd", 1);

  // for visualize
  Pubs["state_overlay"] = nh_.advertise<jsk_rviz_plugins::OverlayText>("/state/overlay_text", 1);
  Pubs["crossroad_marker"] = nh_.advertise<visualization_msgs::MarkerArray>("/state/cross_road_marker", 1);
  Pubs["crossroad_inside_marker"] = nh_.advertise<visualization_msgs::Marker>("/state/cross_inside_marker", 1);
  Pubs["crossroad_bbox"] = nh_.advertise<jsk_recognition_msgs::BoundingBoxArray>("/state/crossroad_bbox", 10);

  // for debug
  Pubs["target_velocity_array"] = nh_.advertise<std_msgs::Float64MultiArray>("/target_velocity_array", 1);
  Pubs["state_local_diffdistance"] = nh_.advertise<std_msgs::Float64>("/state/val_diff_distance", 1);
  Pubs["exectime"] = nh_.advertise<std_msgs::Float64>("/state/exectime", 1);
  // message setup
  state_text_msg.text_size = 18;
  state_text_msg.line_width = 0;
  state_text_msg.font = "DejaVu Sans Mono";
  state_text_msg.width = 500;
  state_text_msg.height = 50;
  state_text_msg.top = 10;
  state_text_msg.left = 10;
  state_text_msg.text = "UNDEFINED";

  // initial publishing state message
  update_msgs();

  // setup a callback for state update();
  setupStateCallback();

  // to move initial state from start state
  // this part confirm broadcasting tf(map to world)
  {
    std::cout << "wait for tf of map to world" << std::endl;
    tf::TransformListener tf;

    tf.waitForTransform("map", "world", ros::Time(), ros::Duration(15));
    if (!ctx->TFInitialized())
      std::cerr << "failed initialization " << std::endl;
  }
  {
    initVectorMapClient();
    if (enableDisplayMarker)
      displayMarker();
  }
}

void DecisionMakerNode::initVectorMap(void)
{
  if (!vector_map_init)
  {
    vMap_mutex.lock();
    if (!vector_map_init && vMap_Areas_flag & vMap_Points_flag & vMap_Lines_flag & vMap_CrossRoads_flag)
    {
      vector_map_init = true;

      int _index = 0;

      // parse vmap
      for (const auto &cross_road : vMap_CrossRoads.data)
      {
        for (const auto &area : vMap_Areas.data)
        {
          if (cross_road.aid == area.aid)
          {
            CrossRoadArea carea;
            carea.id = _index++;
            carea.area_id = area.aid;

            double x_avg = 0.0, x_min = 0.0, x_max = 0.0;
            double y_avg = 0.0, y_min = 0.0, y_max = 0.0;
            double z = 0.0;
            int points_count = 0;

            for (const auto &line : vMap_Lines.data)
            {
              if (area.slid <= line.lid && line.lid <= area.elid)
              {
                geometry_msgs::Point _prev_point;
                for (const auto &point : vMap_Points.data)
                {
                  if (line.fpid <= point.pid && point.pid <= line.fpid)
                  {
                    geometry_msgs::Point _point;
                    _point.x = point.bx;
                    _point.y = point.ly;
                    _point.z = point.h;

                    if (_prev_point.x == _point.x && _prev_point.y == _point.y)
                      continue;

                    _prev_point = _point;
                    points_count++;
                    carea.points.push_back(_point);

                    // calc a centroid point and about intersects size
                    x_avg += _point.x;
                    y_avg += _point.y;
                    x_min = (x_min == 0.0) ? _point.x : std::min(_point.x, x_min);
                    x_max = (x_max == 0.0) ? _point.x : std::max(_point.x, x_max);
                    y_min = (y_min == 0.0) ? _point.y : std::min(_point.y, y_min);
                    y_max = (y_max == 0.0) ? _point.y : std::max(_point.y, y_max);
                    z = _point.z;

                  }  // if pid
                }    // points iter
              }      // if lid
            }        // line iter
            carea.bbox.pose.position.x = x_avg / (double)points_count;
            carea.bbox.pose.position.y = y_avg / (double)points_count;
            carea.bbox.pose.position.z = z;
            carea.bbox.dimensions.x = x_max - x_min;
            carea.bbox.dimensions.y = y_max - y_min;
            carea.bbox.dimensions.z = 2;
            carea.bbox.label = 1;
            intersects.push_back(carea);
          }
        }
      }
    }
    vMap_mutex.unlock();
    Subs["lane_waypoints_array"] =
        nh_.subscribe(TPNAME_BASED_LANE_WAYPOINTS_ARRAY, 100, &DecisionMakerNode::callbackFromLaneWaypoint, this);
  }
}

bool DecisionMakerNode::initVectorMapClient()
{
#ifdef USE_VMAP_SERVER  // This is not successfully run due to failing vmap
  // server

  vector_map::VectorMap vmap;
  vmap.subscribe(nh_, vector_map::Category::AREA, ros::Duration(0));

  cross_road_srv.request.pose = current_pose_;
  cross_road_srv.request.waypoints = current_finalwaypoints_;

  cross_road_cli = nh_.serviceClient<vector_map_server::GetCrossRoad>("vector_map_server/get_cross_road");

  return cross_road_cli.call(cross_road_srv);
#endif
  return false;
}
}
