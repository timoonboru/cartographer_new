/*
 * Copyright 2016 The Cartographer Authors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "cartographer_ros/sensor_bridge.h"

#include "cartographer/common/make_unique.h"
#include "cartographer_ros/msg_conversion.h"
#include "cartographer_ros/time_conversion.h"

namespace cartographer_ros {

namespace carto = ::cartographer;

using carto::transform::Rigid3d;

namespace {

const std::string& CheckNoLeadingSlash(const std::string& frame_id) {
  if (frame_id.size() > 0) {
    CHECK_NE(frame_id[0], '/') << "The frame_id " << frame_id
                               << " should not start with a /. See 1.7 in "
                                  "http://wiki.ros.org/tf2/Migration.";
  }
  return frame_id;
}

}  // namespace

#define PI 3.1415926535897931160

typedef pcl::PointXYZI PointType;

inline double rad2deg(double radians)
{
  return radians * 180.0 / M_PI;
}

inline double deg2rad(double degrees)
{
  return degrees * M_PI / 180.0;
}


static double adjlon(double lon)
{
    const double SPI = 3.14159265359;
  if (fabs(lon) <= SPI) return( lon );
  const double ONEPI = 3.14159265358979323846;
  lon += ONEPI;  /* adjust to 0..2pi rad */
  const double TWOPI = 6.2831853071795864769;
  lon -= TWOPI * floor(lon / TWOPI); /* remove integral # of 'revolutions'*/
  lon -= ONEPI;  /* adjust back to -pi..pi rad */
  return( lon );
}

static void toSM_ECC(double lat, double lon, double lat0, double lon0, double *x, double *y)
{
    const double WGSinvf = 298.257223563;
  const double f = 1.0 / WGSinvf;       // WGS84 ellipsoid flattening parameter
  const double e2 = 2 * f - f * f;      // eccentricity^2  .006700
  const double e = sqrt(e2);
  const double WGS84_semimajor_axis_meters = 6378137.0;
  const double mercator_k0 = 0.9996;
  const double z = WGS84_semimajor_axis_meters * mercator_k0;
  const double DEGREE = (PI/180.0);
  *x = (lon - lon0) * DEGREE * z;

    // y =.5 ln( (1 + sin t) / (1 - sin t) )
  const double s = sin(lat * DEGREE);
        //const double y3 = (.5 * log((1 + s) / (1 - s))) * z;

  const double s0 = sin(lat0 * DEGREE);
        //const double y30 = (.5 * log((1 + s0) / (1 - s0))) * z;
        //const double y4 = y3 - y30;

      //Add eccentricity terms

  const double falsen =  z *log(tan(PI/4 + lat0 * DEGREE / 2)*pow((1. - e * s0)/(1. + e * s0), e/2.));
  const double test =    z *log(tan(PI/4 + lat  * DEGREE / 2)*pow((1. - e * s )/(1. + e * s ), e/2.));
  *y = test - falsen;
}


static double DistGreatCircle(double slat, double slon, double dlat, double dlon)
{
//    double th1,costh1,sinth1,sina12,cosa12,M,N,c1,c2,D,P,s1;
//    int merid, signS;

  /*   Input/Output from geodesic functions   */
  double al12;           /* Forward azimuth */
  double al21;           /* Back azimuth    */
  double geod_S;         /* Distance        */
  double phi1, lam1, phi2, lam2;

  int ellipse;
  double geod_f;
  double geod_a;
  double es, onef, f, f64, f2, f4;

  double d5;

  const double DEGREE = (PI/180.0);

  phi1 = slat * DEGREE;
  lam1 = slon * DEGREE;
  phi2 = dlat * DEGREE;
  lam2 = dlon * DEGREE;

  //void geod_inv(struct georef_state *state)
  {
    double      th1,th2,thm,dthm,dlamm,dlam,sindlamm,costhm,sinthm,cosdthm,
    sindthm,L,E,cosd,d,X,Y,T,sind,tandlammp,u,v,D,A,B;


    /*   Stuff the WGS84 projection parameters as necessary
     *      To avoid having to include <geodesic,h>
     */

    ellipse = 1;
    const double WGSinvf = 298.257223563;
    f = 1.0 / WGSinvf;       /* WGS84 ellipsoid flattening parameter */
    const double WGS84_semimajor_axis_meters = 6378137.0;
    geod_a = WGS84_semimajor_axis_meters;

    es = 2 * f - f * f;
    onef = sqrt(1. - es);
    geod_f = 1 - onef;
    f2 = geod_f/2;
    f4 = geod_f/4;
    f64 = geod_f*geod_f/64;

    if (ellipse) {
      th1 = atan(onef * tan(phi1));
      th2 = atan(onef * tan(phi2));
    } else {
      th1 = phi1;
      th2 = phi2;
    }
    thm = .5 * (th1 + th2);
    dthm = .5 * (th2 - th1);
    dlamm = .5 * ( dlam = adjlon(lam2 - lam1) );
    const double DTOL = 1e-12;
    if (fabs(dlam) < DTOL && fabs(dthm) < DTOL) {
      al12 =  al21 = geod_S = 0.;
      return 0.0;
    }
    sindlamm = sin(dlamm);
    costhm = cos(thm);      sinthm = sin(thm);
    cosdthm = cos(dthm);    sindthm = sin(dthm);
    L = sindthm * sindthm + (cosdthm * cosdthm - sinthm * sinthm)
                    * sindlamm * sindlamm;
    d = acos(cosd = 1 - L - L);
    if (ellipse) 
    {
      E = cosd + cosd;
      sind = sin( d );
      Y = sinthm * cosdthm;
      Y *= (Y + Y) / (1. - L);
      T = sindthm * costhm;
      T *= (T + T) / L;
      X = Y + T;
      Y -= T;
      T = d / sind;
      D = 4. * T * T;
      A = D * E;
      B = D + D;
      geod_S = geod_a * sind * (T - f4 * (T * X - Y) +
                                  f64 * (X * (A + (T - .5 * (A - E)) * X) -
                                  Y * (B + E * Y) + D * X * Y));
      tandlammp = tan(.5 * (dlam - .25 * (Y + Y - E * (4. - X)) *
                                  (f2 * T + f64 * (32. * T - (20. * T - A)
                                  * X - (B + 4.) * Y)) * tan(dlam)));
    } else {
      geod_S = geod_a * d;
      tandlammp = tan(dlamm);
    }
    u = atan2(sindthm , (tandlammp * costhm));
    v = atan2(cosdthm , (tandlammp * sinthm));
    const double TWOPI = 6.2831853071795864769;
    al12 = adjlon(TWOPI + v - u);
    al21 = adjlon(TWOPI - v - u);
    }

  d5 = geod_S / 1852.0;

  return d5;
}

static void calDistanceStatic(double lat0, double lon0, double lat1, double lon1, double *brg, double *dist)
{
  double lon0x = lon0;
  double lon1x = lon1;

  //    Make lon points the same phase
  if((lon0x * lon1x) < 0.)
  {
      lon0x < 0.0 ? lon0x += 360.0 : lon1x += 360.0;
              //    Choose the shortest distance
    if(fabs(lon0x - lon1x) > 180.)
    {
        lon0x > lon1x ? lon0x -= 360.0 : lon1x -= 360.0;
    }

  //    Make always positive
  lon1x += 360.;
  lon0x += 360.;
  }

        //    Classic formula, which fails for due east/west courses....
  if(dist)
  {
    //    In the case of exactly east or west courses
    //    we must make an adjustment if we want true Mercator distances

    //    This idea comes from Thomas(Cagney)
    //    We simply require the dlat to be (slightly) non-zero, and carry on.
    //    MAS022210 for HamishB from 1e-4 && .001 to 1e-9 for better precision
    //    on small latitude diffs
    const double mlat0 = fabs(lat1 - lat0) < 1e-9 ? lat0 + 1e-9 : lat0;

    double east, north;
    toSM_ECC(lat1, lon1x, mlat0, lon0x, &east, &north);
    const double C = atan2(east, north);
    if(cos(C))
    {
      const double dlat = (lat1 - mlat0) * 60.;              // in minutes
      *dist = (dlat /cos(C));
        }
    else
    {
        *dist = DistGreatCircle(lat0, lon0, lat1, lon1);
    }
  }

        //    Calculate the bearing using the un-adjusted original latitudes and Mercator Sailing
  if(brg)
  {
    double east, north;
    toSM_ECC(lat1, lon1x, lat0, lon0x, &east, &north);

    const double C = atan2(east, north);
    const double brgt = 180. + (C * 180. / PI);
    if (brgt < 0)
      *brg = brgt + 360.;
        else if (brgt >= 360.)
      *brg = brgt - 360.;
        else
      *brg = brgt;
    }
}

static void angleConversion(double &angle)
{
  int intenger = 0;

  if(angle < 0)
  {
        intenger = fabs(angle) / 360;
    angle += 360 *(intenger+1);
    }
    else if(angle >= 360)
    {
    intenger = angle/360;
    angle -= 360*intenger;
  }
}

SensorBridge::SensorBridge(
    const int num_subdivisions_per_laser_scan,
    const std::string& tracking_frame,
    const double lookup_transform_timeout_sec, tf2_ros::Buffer* const tf_buffer,
    carto::mapping::TrajectoryBuilder* const trajectory_builder)
    : num_subdivisions_per_laser_scan_(num_subdivisions_per_laser_scan),
      tf_bridge_(tracking_frame, lookup_transform_timeout_sec, tf_buffer),
      trajectory_builder_(trajectory_builder) {}

std::unique_ptr<::cartographer::sensor::OdometryData>
SensorBridge::ToOdometryData(const nav_msgs::Odometry::ConstPtr& msg) {
  const carto::common::Time time = FromRos(msg->header.stamp);
  //const auto sensor_to_tracking = tf_bridge_.LookupToTracking(
  //    time, CheckNoLeadingSlash(msg->child_frame_id));
  //if (sensor_to_tracking == nullptr) {
  //  return nullptr;
  //}


  real_time_lat_ = (double)msg->pose.pose.position.x;
  real_time_lon_ = (double)msg->pose.pose.position.z;  

  if(first_tag_gps_)
   {
    if((real_time_lat_!= 0)||(real_time_lon_!= 0))
    {
      first_lat_ = real_time_lat_;
      first_lon_ = real_time_lon_;
      first_tag_gps_ = false;
    }
  }

  double dist = 0;
  double angle = 0;
  calDistanceStatic(real_time_lat_, real_time_lon_, first_lat_, first_lon_, &angle, &dist);

  dist *= 1852;
      
  double yaw_first_time_orientiation_ =
    ::cartographer::transform::GetYaw(first_time_orientiation_);

  double angle_diff = 0;

  angle_diff = angle;
  double relative_pose[2] = {0,0};

  relative_pose[0] = dist * sin(angle_diff*M_PI/180.0  + M_PI/2 );
  relative_pose[1] = dist * cos(angle_diff*M_PI/180.0  + M_PI/2 );

  //LOG(INFO) << "relative_pose[0] = "<< relative_pose[0] << "  relative_pose[1] = " << relative_pose[1];

  return ::cartographer::common::make_unique<
      ::cartographer::sensor::OdometryData>(
      ::cartographer::sensor::OdometryData{
          //time, ToRigid3d(msg->pose.pose) * sensor_to_tracking->inverse()});
          time, carto::transform::Rigid3d({relative_pose[0],relative_pose[1],0},
          msg_orientiation_) }); //mnf
}

void SensorBridge::HandleOdometryMessage(
    const std::string& sensor_id, const nav_msgs::Odometry::ConstPtr& msg) {
  std::unique_ptr<::cartographer::sensor::OdometryData> odometry_data =
      ToOdometryData(msg);
  if (odometry_data != nullptr) {
    trajectory_builder_->AddOdometerData(sensor_id, odometry_data->time,
                                         odometry_data->pose);
  }
}

std::unique_ptr<::cartographer::sensor::ImuData> SensorBridge::ToImuData(
    const sensor_msgs::Imu::ConstPtr& msg) {
  CHECK_NE(msg->linear_acceleration_covariance[0], -1)
      << "Your IMU data claims to not contain linear acceleration measurements "
         "by setting linear_acceleration_covariance[0] to -1. Cartographer "
         "requires this data to work. See "
         "http://docs.ros.org/api/sensor_msgs/html/msg/Imu.html.";
  CHECK_NE(msg->angular_velocity_covariance[0], -1)
      << "Your IMU data claims to not contain angular velocity measurements "
         "by setting angular_velocity_covariance[0] to -1. Cartographer "
         "requires this data to work. See "
         "http://docs.ros.org/api/sensor_msgs/html/msg/Imu.html.";

  const carto::common::Time time = FromRos(msg->header.stamp);
  const auto sensor_to_tracking = tf_bridge_.LookupToTracking(
      time, CheckNoLeadingSlash(msg->header.frame_id));
  if (sensor_to_tracking == nullptr) {
    return nullptr;
  }
  CHECK(sensor_to_tracking->translation().norm() < 1e-5)
      << "The IMU frame must be colocated with the tracking frame. "
         "Transforming linear acceleration into the tracking frame will "
         "otherwise be imprecise.";

  msg_orientiation_ = ToEigen(msg->orientation);
  if(first_tag_imu_)
  {
    first_time_orientiation_ = msg_orientiation_;
    first_tag_imu_ = false;
  }
  if((msg_orientiation_.x() != 0) || (msg_orientiation_.y() != 0) || (msg_orientiation_.z() != 0))
    real_time_orientiation_ = first_time_orientiation_.inverse() * msg_orientiation_ ;

  //double yaw_first_time_orientiation_ =
  //    ::cartographer::transform::GetYaw(real_time_orientiation_) * 180 / 3.14159;

  //LOG(INFO)<<"yaw_first_time_orientiation_"<<yaw_first_time_orientiation_;

  return ::cartographer::common::make_unique<::cartographer::sensor::ImuData>(
      ::cartographer::sensor::ImuData{
          time,
          sensor_to_tracking->rotation() * ToEigen(msg->linear_acceleration),
          sensor_to_tracking->rotation() * ToEigen(msg->angular_velocity),
          //sensor_to_tracking->rotation() * ToEigen(msg->orientation)});
          sensor_to_tracking->rotation() * msg_orientiation_}); //mnf
  
}

void SensorBridge::HandleImuMessage(const std::string& sensor_id,
                                    const sensor_msgs::Imu::ConstPtr& msg) {
  std::unique_ptr<::cartographer::sensor::ImuData> imu_data = ToImuData(msg);
  if (imu_data != nullptr) {
    trajectory_builder_->AddImuData(sensor_id, imu_data->time,
                                    imu_data->linear_acceleration,
                                    imu_data->angular_velocity,
                                    imu_data->orientation);
  }
}

void SensorBridge::HandleLaserScanMessage(
    const std::string& sensor_id, const sensor_msgs::LaserScan::ConstPtr& msg) {
  HandleLaserScan(sensor_id, FromRos(msg->header.stamp), msg->header.frame_id,
                  ToPointCloudWithIntensities(*msg));
}

void SensorBridge::HandleMultiEchoLaserScanMessage(
    const std::string& sensor_id,
    const sensor_msgs::MultiEchoLaserScan::ConstPtr& msg) {
  HandleLaserScan(sensor_id, FromRos(msg->header.stamp), msg->header.frame_id,
                  ToPointCloudWithIntensities(*msg));
}

void SensorBridge::HandlePointCloud2Message(
    const std::string& sensor_id,
    const sensor_msgs::PointCloud2::ConstPtr& msg) {
  pcl::PointCloud<pcl::PointXYZ> pcl_point_cloud;
  pcl::fromROSMsg(*msg, pcl_point_cloud);
  carto::sensor::TimedPointCloud point_cloud;
  for (const auto& point : pcl_point_cloud) {
    point_cloud.emplace_back(point.x, point.y, point.z, 0.f);
  }
  HandleRangefinder(sensor_id, FromRos(msg->header.stamp), msg->header.frame_id,
                    point_cloud);
}

const TfBridge& SensorBridge::tf_bridge() const { return tf_bridge_; }

void SensorBridge::HandleLaserScan(
    const std::string& sensor_id, const carto::common::Time start_time,
    const std::string& frame_id,
    const carto::sensor::PointCloudWithIntensities& points) {
  // TODO(gaschler): Use per-point time instead of subdivisions.
  for (int i = 0; i != num_subdivisions_per_laser_scan_; ++i) {
    const size_t start_index =
        points.points.size() * i / num_subdivisions_per_laser_scan_;
    const size_t end_index =
        points.points.size() * (i + 1) / num_subdivisions_per_laser_scan_;
    const carto::sensor::TimedPointCloud subdivision(
        points.points.begin() + start_index, points.points.begin() + end_index);
    if (start_index == end_index) {
      continue;
    }
    const size_t middle_index = (start_index + end_index) / 2;
    const carto::common::Time subdivision_time =
        start_time +
        carto::common::FromSeconds(points.points.at(middle_index)[3]);
    HandleRangefinder(sensor_id, subdivision_time, frame_id, subdivision);
  }
}

void SensorBridge::HandleRangefinder(
    const std::string& sensor_id, const carto::common::Time time,
    const std::string& frame_id, const carto::sensor::TimedPointCloud& ranges) {
  const auto sensor_to_tracking =
      tf_bridge_.LookupToTracking(time, CheckNoLeadingSlash(frame_id));
  if (sensor_to_tracking != nullptr) {
    trajectory_builder_->AddRangefinderData(
        sensor_id, time, sensor_to_tracking->translation().cast<float>(),
        carto::sensor::TransformTimedPointCloud(
            ranges, sensor_to_tracking->cast<float>()));
  }
}

}  // namespace cartographer_ros
