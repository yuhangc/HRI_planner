//----------------------------------------------------------------------------------
//
// Human Robot Interaction Planning Framework
//
// Created on   : 12/14/2017
// Last revision: 01/30/2018
// Author       : Che, Yuhang <yuhangc@stanford.edu>
// Contact      : Che, Yuhang <yuhangc@stanford.edu>
//
//----------------------------------------------------------------------------------

#include <fstream>
#include <sstream>

#include "top_view_tracker/hat_tracker.h"

namespace tracking {

//----------------------------------------------------------------------------------
void wrap_to_pi(double &ang)
{
    while (ang >= PI)
        ang -= PI2;
    while (ang < -PI)
        ang += PI2;
}

//----------------------------------------------------------------------------------
void correct_rot_meas_range(const double ref, double &meas)
{
    double diff = meas - ref;
    wrap_to_pi(diff);
    meas = ref + diff;
}

//----------------------------------------------------------------------------------
void HatTracker::load_config(const std::string &path)
{
    // load configure file (JSON)
    std::ifstream config_file(path, std::ifstream::binary);
    Json::Value root;

    config_file >> root;

    // read number of hats to track
    n_hats_ = root["num_hats"].asInt();

    // load hat templates
    for (int id = 0; id < n_hats_; id++) {
        HatTemplate new_hat;

        std::stringstream ss;
        ss << "hat" << id;

        new_hat.hat_hsv_low = cv::Scalar(root[ss.str()]["hat_hsv_low"][0].asDouble(),
                                         root[ss.str()]["hat_hsv_low"][1].asDouble(),
                                         root[ss.str()]["hat_hsv_low"][2].asDouble());

        new_hat.hat_hsv_high = cv::Scalar(root[ss.str()]["hat_hsv_high"][0].asDouble(),
                                          root[ss.str()]["hat_hsv_high"][1].asDouble(),
                                          root[ss.str()]["hat_hsv_high"][2].asDouble());

        new_hat.cap_hsv_low = cv::Scalar(root[ss.str()]["cap_hsv_low"][0].asDouble(),
                                         root[ss.str()]["cap_hsv_low"][1].asDouble(),
                                         root[ss.str()]["cap_hsv_low"][2].asDouble());

        new_hat.cap_hsv_high = cv::Scalar(root[ss.str()]["cap_hsv_high"][0].asDouble(),
                                          root[ss.str()]["cap_hsv_high"][1].asDouble(),
                                          root[ss.str()]["cap_hsv_high"][2].asDouble());

        new_hat.hat_size = root[ss.str()]["hat_size"].asInt();
        new_hat.cap_size = root[ss.str()]["cap_size"].asInt();

        new_hat.hat_area = (int) (new_hat.hat_size * new_hat.hat_size * 3.14 / 4.0);
        new_hat.cap_area = (int) (new_hat.cap_size * new_hat.cap_size * 2.0);

        hat_temps_.push_back(new_hat);
    }

    // frequency
    double fps = root["FPS"].asDouble();
    dt_ = 1.0 / fps;

    // detection ratio thresholds
    ratio_th_high_hat_ = root["ratio_threshold_high_hat"].asDouble();
    ratio_th_low_hat_ = root["ratio_threshold_low_hat"].asDouble();
    ratio_th_high_cap_ = root["ratio_threshold_high_cap"].asDouble();
    ratio_th_low_cap_ = root["ratio_threshold_low_cap"].asDouble();

    // read in process noise and measurement noise matrices from config file
    double process_noise_pos = root["process_noise_pos"].asDouble();
    double process_noise_vel = root["process_noise_vel"].asDouble();
    meas_noise_base_ = root["measurement_noise_base"].asDouble();
    rot_meas_noise_base_ = root["rot_meas_noise_base"].asDouble();
    rot_vel_noise_base_ = root["rot_vel_noise_base"].asDouble();

    // create tracker instances
    int state_size = 4;
    int meas_size = 2;
    int ctrl_size = 2;

    int rot_state_size = 2;
    int rot_meas_size = 2;
    int rot_ctrl_size = 2;

    pos_trackers_.clear();
    for (int id = 0; id < n_hats_; id++) {
        // create filter
        cv::Ptr<cv::KalmanFilter> new_tracker = new cv::KalmanFilter(state_size, meas_size, ctrl_size, CV_64F);

        // set transition matrix
        cv::setIdentity(new_tracker->transitionMatrix);
        new_tracker->transitionMatrix.at<double>(0, 2) = dt_;
        new_tracker->transitionMatrix.at<double>(1, 3) = dt_;

        // set measurement matrix
        new_tracker->measurementMatrix = cv::Mat::zeros(meas_size, state_size, CV_64F);
        new_tracker->measurementMatrix.at<double>(0, 0) = 1.0;
        new_tracker->measurementMatrix.at<double>(1, 1) = 1.0;

        // process noise covariance matrix
        //! measurement noise will be updated dynamically
        cv::setIdentity(new_tracker->processNoiseCov.rowRange(0, 2).colRange(0, 2), process_noise_pos);
        cv::setIdentity(new_tracker->processNoiseCov.rowRange(2, 4).colRange(2, 4), process_noise_vel);

        pos_trackers_.push_back(new_tracker);

        // create filter for rotation
        cv::Ptr<cv::KalmanFilter> rot_tracker = new cv::KalmanFilter(rot_state_size, rot_meas_size,
                                                                     rot_ctrl_size, CV_64F);

        // set transition matrix
        cv::setIdentity(rot_tracker->transitionMatrix);
        rot_tracker->transitionMatrix.at<double>(0, 1) = dt_;

        // set measurement matrix
        rot_tracker->measurementMatrix = cv::Mat::zeros(rot_meas_size, rot_state_size, CV_64F);
        rot_tracker->measurementMatrix.at<double>(0, 0) = 1.0;
        rot_tracker->measurementMatrix.at<double>(1, 0) = 1.0;

        // process noise
        //! measurement noise will be updated dynamically
        cv::setIdentity(rot_tracker->processNoiseCov, 0.01);
        rot_tracker->processNoiseCov.at<double>(1, 1) = 5.0;

        rot_trackers_.push_back(rot_tracker);
    }

    // set default scale
    disp_scale_ = 1.0;

    // initialize flags
    for (int id = 0; id < n_hats_; id++) {
        nframes_lost_.push_back(0);
        flag_hat_initialized_.push_back(false);
        flag_hat_lost_.push_back(false);
    }
}

//----------------------------------------------------------------------------------
void HatTracker::track(const cv::Mat im_in, bool flag_vis)
{
    // update frame
    frame_ = im_in;

    // show detection
    cv::Mat im_out = im_in.clone();

    // track all hats
    for (int id = 0; id < n_hats_; id++) {
        if (!flag_hat_initialized_[id] || flag_hat_lost_[id]) {
            if (detect_and_init_hat(id, im_out)) {
                nframes_lost_[id] = 0;
                flag_hat_initialized_[id] = true;
                flag_hat_lost_[id] = false;
            }
        } else {
            // obtain prediction with Kalman filter
            cv::Mat hat_state = pos_trackers_[id]->predict();

            // define ROI based on prediction
            // FIXME: for now use a constant ROI size
            cv::Rect roi;
            roi.width = hat_temps_[id].hat_size << 1;
            roi.height = hat_temps_[id].hat_size << 1;
            roi.x = (int) (hat_state.at<double>(0) - roi.width / 2.0);
            roi.y = (int) (hat_state.at<double>(1) - roi.height / 2.0);

            // try to detect the hat within roi
            cv::Rect hat_detection;
            double hat_qual = detect_hat_top(hat_temps_[id], roi, hat_detection);
            double cap_qual = -1.0;

            cv::Vec2d hat_center;
            cv::Vec2d cap_center;

            if (hat_qual < 0) {
                std::cout << "Cannot find hat " << id << " !!!!!" << std::endl;
                ++nframes_lost_[id];
                if (nframes_lost_[id] > 30) {
                    flag_hat_lost_[id] = true;
                    flag_hat_initialized_[id] = false;
                }
            } else {
                // transform to global image coordinates
                hat_detection.x += roi.x;
                hat_detection.y += roi.y;

                // dynamically update the measurement covariance matrix
                set_kf_cov(hat_qual, pos_trackers_[id]->measurementNoiseCov);

                // update Kalman filter
                hat_center = rect_center(hat_detection);
                pos_trackers_[id]->correct(cv::Mat(hat_center));

                // try to detect hat cap
                cv::Rect cap_detection;
                get_cap_roi(hat_detection, hat_qual, roi);
                cap_qual = detect_hat_cap(hat_temps_[id], roi, cap_detection);

                if (cap_qual < 0) {
                    std::cout << "Cannot find cap " << id << " !!!!!" << std::endl;
                }

                // transform to global image coordinates
                cap_detection.x += roi.x;
                cap_detection.y += roi.y;

                cap_center = rect_center(cap_detection);

                // draw the detections
                if (flag_vis && cap_qual > 0) {
                    cv::rectangle(im_out, cap_detection, CV_RGB(0, 0, 255), 2);
                }
            }

            // TODO: update cap and orientation tracking
            cv::Mat hat_rot = rot_trackers_[id]->predict();

            // obtain the measurement from cap detection
            cv::Mat rot_meas(2, 1, CV_64F);
            if (hat_qual > 0 && cap_qual > 0) {
                rot_meas.at<double>(0) = std::atan2(cap_center[1] - hat_center[1],
                                                    cap_center[0] - hat_center[0]);
            } else {
                rot_meas.at<double>(0) = hat_rot.at<double>(0);
            }

            // measurement from velocity of hat
            const cv::Mat vel = pos_trackers_[id]->statePost.rowRange(2, 4);
            rot_meas.at<double>(1) = std::atan2(vel.at<double>(1), vel.at<double>(0));

            // correct the measurements
            correct_rot_meas_range(hat_rot.at<double>(0), rot_meas.at<double>(0));
            correct_rot_meas_range(hat_rot.at<double>(0), rot_meas.at<double>(1));

//            std::cout << "prediction:" << std::endl;
//            std::cout << hat_rot << std::endl;
//            std::cout << "measurement:" << std::endl;
//            std::cout << rot_meas << std::endl;
            // get covariance of the measurements
            set_rot_kf_cov(cap_qual, vel,
                           pos_trackers_[id]->errorCovPost.rowRange(2, 4).colRange(2, 4),
                           rot_trackers_[id]->measurementNoiseCov);

//            std::cout << "prediction covariance" << std::endl;
//            std::cout << rot_trackers_[id]->errorCovPre << std::endl;
//            std::cout << "measurement covariance" << std::endl;
//            std::cout << rot_trackers_[id]->measurementNoiseCov << std::endl;
//
//            std::cout << "transition matrix" << std::endl;
//            std::cout << rot_trackers_[id]->transitionMatrix << std::endl;
//            std::cout << "measurement matrix:" << std::endl;
//            std::cout << rot_trackers_[id]->measurementMatrix << std::endl;
//            std::cout << "state pre:" << std::endl;
//            std::cout << rot_trackers_[id]->statePre << std::endl;
//            std::cout << "cov pre:" << std::endl;
//            std::cout << rot_trackers_[id]->errorCovPre << std::endl;
            // measurement update
            rot_trackers_[id]->correct(rot_meas);
            wrap_to_pi(rot_trackers_[id]->statePost.at<double>(0));

            // draw the orientation
            cv::Point2d pt1, pt2;
            pt1.x = pos_trackers_[id]->statePost.at<double>(0);
            pt1.y = pos_trackers_[id]->statePost.at<double>(1);
            pt2.x = pt1.x + 20.0 * std::cos(rot_trackers_[id]->statePost.at<double>(0));
            pt2.y = pt1.y + 20.0 * std::sin(rot_trackers_[id]->statePost.at<double>(0));

            // draw estimation
            if (flag_vis) {
                cv::Rect hat_pred;
                hat_pred.width = hat_temps_[id].hat_size;
                hat_pred.height = hat_temps_[id].hat_size;
                hat_pred.x = (int) pos_trackers_[id]->statePost.at<double>(0) - hat_pred.width >> 1;
                hat_pred.y = (int) pos_trackers_[id]->statePost.at<double>(1) - hat_pred.height >> 1;

                cv::rectangle(im_out, hat_detection, CV_RGB(255,0,0), 2);
            }

            cv::arrowedLine(im_out, pt1, pt2, CV_RGB(0, 255, 0), 2);
        }
    }

    // display detection result if applicable
    if (flag_vis) {
        cv::resize(im_out, im_out, cv::Size(), disp_scale_, disp_scale_);
        cv::imshow("detection", im_out);
        cv::waitKey(1);
    }
}

//----------------------------------------------------------------------------------
void HatTracker::get_tracking(std::vector<cv::Mat> &pose, std::vector<cv::Mat> &vel, std::vector<int> &hat_id)
{
    for (int id = 0; id < n_hats_; id++) {
        // hat has to be initialized first
        if (flag_hat_initialized_[id]) {
            hat_id.push_back(id);

            cv::Mat hat_pose(3, 1, CV_64F);
            hat_pose.at<double>(0) = pos_trackers_[id]->statePost.at<double>(0);
            hat_pose.at<double>(1) = pos_trackers_[id]->statePost.at<double>(1);
            hat_pose.at<double>(2) = rot_trackers_[id]->statePost.at<double>(0);
            pose.push_back(hat_pose);

            cv::Mat hat_vel(3, 1, CV_64F);
            hat_vel.at<double>(0) = pos_trackers_[id]->statePost.at<double>(2);
            hat_vel.at<double>(1) = pos_trackers_[id]->statePost.at<double>(3);
            hat_vel.at<double>(2) = rot_trackers_[id]->statePost.at<double>(1);
            vel.push_back(hat_vel);
        }
    }
}

//----------------------------------------------------------------------------------
bool HatTracker::detect_and_init_hat(const int &id, cv::Mat im_out)
{
    int margin = 150;

    // detect hat top from the entire image
    cv::Rect hat_detection;
    cv::Rect roi(margin, 0, frame_.cols - (margin << 1), frame_.rows);
    const double hat_qual = detect_hat_top(hat_temps_[id], roi, hat_detection);

    hat_detection.x += margin;

    if (hat_qual < 0) {
        std::cout << "Cannot find hat!" << std::endl;
        return false;
    }

    // detect hat cap from the surrounding of the detect hat
    cv::Rect cap_detection;
    get_cap_roi(hat_detection, hat_qual, roi);
    const double cap_qual = detect_hat_cap(hat_temps_[id], roi, cap_detection);

    if (cap_qual < 0) {
        std::cout << "Cannot find cap " << id << " for initialization!" << std::endl;
        return false;
    }

    // tranform back to global image coordinate
    cap_detection.x += roi.x;
    cap_detection.y += roi.y;

    // calculate the centers
    cv::Vec2d hat_center = rect_center(hat_detection);
    cv::Vec2d cap_center = rect_center(cap_detection);

    // initialize the Kalman Filter
    pos_trackers_[id]->statePost.at<double>(0) = hat_center[0];
    pos_trackers_[id]->statePost.at<double>(1) = hat_center[1];
    pos_trackers_[id]->statePost.at<double>(2) = 0.0;
    pos_trackers_[id]->statePost.at<double>(3) = 0.0;

    set_kf_cov(hat_qual, pos_trackers_[id]->errorCovPost);
    pos_trackers_[id]->errorCovPost.at<double>(2, 2) = 1e2;
    pos_trackers_[id]->errorCovPost.at<double>(3, 3) = 1e2;

    // TODO: initialize orientation/cap tracking
    rot_trackers_[id]->statePost.at<double>(0) = std::atan2(cap_center[1] - hat_center[1],
                                                            cap_center[0] - hat_center[0]);
    rot_trackers_[id]->statePost.at<double>(1) = 0.0;

    cv::setIdentity(rot_trackers_[id]->errorCovPost);
    rot_trackers_[id]->errorCovPost.at<double>(0, 0) = rot_meas_noise_base_ / cap_qual;
    rot_trackers_[id]->errorCovPost.at<double>(1, 1) = 1e2;

    // draw the bounding boxes
    if (!im_out.empty()) {
        cv::rectangle(im_out, hat_detection, CV_RGB(255,0,0), 2);
        cv::rectangle(im_out, cap_detection, CV_RGB(0, 0, 255), 2);
    }

    return true;
}

////----------------------------------------------------------------------------------
//void HatTracker::detect_and_init_hats()
//{
//    // detect all hats
//    for (int id = 0; id < n_hats_; id++)
//        detect_and_init_hat(id);
//}

//----------------------------------------------------------------------------------
double HatTracker::detect_hat_top(const HatTemplate &hat_temp, cv::Rect &roi, cv::Rect &detection)
{
    clip_roi(roi);

    cv::Mat im = frame_(roi);
    std::vector<std::vector<cv::Point>> contours;

    detect_hat_preprocess(im, hat_temp.hat_hsv_low, hat_temp.hat_hsv_high, contours);

    // report lost if no contours found
    if (contours.empty()) {
        return -1;
    }

    // find the contour that is closest to the size of the cap, and within threshold
    double size_th_low = ratio_th_low_hat_ * hat_temp.hat_area;
    double size_th_high = ratio_th_high_hat_ * hat_temp.hat_area;
    double min_diff = 1e6;
    double area_detection = 0;

    for (auto &ct : contours) {
        // check size
        const double area = cv::contourArea(ct);
        if (area < size_th_low || area > size_th_high)
            continue;

        // check shape
        cv::Rect bb = cv::boundingRect(ct);
        const double ratio = (double) bb.height / (double) bb.width;
        if (ratio > 2 || ratio < 0.5)
            continue;

        const double diff = std::abs(area - hat_temp.hat_area);
        if (diff < min_diff) {
            min_diff = diff;
            area_detection = area;
            detection = bb;
        }
    }

    // if not found
    if (min_diff < 1e6)
        return area_detection / hat_temp.hat_area;
    else
        return -1;
}

//----------------------------------------------------------------------------------
double HatTracker::detect_hat_cap(const HatTemplate &hat_temp, cv::Rect &roi, cv::Rect &detection)
{
    clip_roi(roi);

    cv::Mat im = frame_(roi);
    std::vector<std::vector<cv::Point>> contours;

    detect_hat_preprocess(im, hat_temp.cap_hsv_low, hat_temp.cap_hsv_high, contours);

    // report lost if no contours found
    if (contours.empty()) {
        return -1;
    }

    // find the contour that is closest to the size of the cap, and within threshold
    double size_th_low = ratio_th_low_cap_ * hat_temp.cap_area;
    double size_th_high = ratio_th_high_cap_ * hat_temp.cap_area;
    double min_diff = 1e6;
    double area_detection = 0;

    for (auto &ct : contours) {
        // check size
        const double area = cv::contourArea(ct);
        if (area < size_th_low || area > size_th_high)
            continue;

        const double diff = std::abs(area - hat_temp.cap_area);
        if (diff < min_diff) {
            min_diff = diff;
            area_detection = area;
            detection = cv::boundingRect(ct);
        }
    }

    // if not found
    if (min_diff < 1e6)
        return area_detection / hat_temp.cap_area;
    else
        return -1;
}

//----------------------------------------------------------------------------------
void HatTracker::detect_hat_preprocess(const cv::Mat &im, const cv::Scalar &lb, const cv::Scalar &ub,
                                       std::vector<std::vector<cv::Point>> &contours)
{
    cv::Mat img_hsv;

    // convert to hsv color space
    cv::cvtColor(im, img_hsv, CV_BGR2HSV);

//    cv::imshow("process", im);
//    cv::waitKey(0);

    // create mask based on hat template
    cv::Mat color_mask;
    cv::inRange(img_hsv, lb, ub, color_mask);

//    cv::imshow("process", color_mask);
//    cv::waitKey(10);

    // filtering
    // opening filter to remove small objects (false positives)
    cv::erode(color_mask, color_mask, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(2, 2)));
    cv::dilate(color_mask, color_mask, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(2, 2)));
    cv::dilate(color_mask, color_mask, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(2, 2)));

    // closing to remove small holes (false negatives)
    cv::dilate(color_mask, color_mask, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(2, 2)));
    cv::erode(color_mask, color_mask, cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(2, 2)));

//    cv::imshow("process", color_mask);
//    cv::waitKey(1);

    // find contours in the image
    std::vector<cv::Vec4i> hierarchy;

    cv::findContours(color_mask, contours, hierarchy, CV_RETR_EXTERNAL, CV_CHAIN_APPROX_SIMPLE);
}

//----------------------------------------------------------------------------------
void HatTracker::get_cap_roi(const cv::Rect &hat_detection, const double qual, cv::Rect &cap_roi)
{
    const double s = std::sqrt(qual);
    cap_roi.width = (int) ((hat_detection.width << 1) / s);
    cap_roi.height = (int) ((hat_detection.height << 1) / s);

    cap_roi.x = std::max(0, hat_detection.x + ((hat_detection.width - cap_roi.width) >> 1));
    cap_roi.y = std::max(0, hat_detection.y + ((hat_detection.height - cap_roi.height) >> 1));
}

//----------------------------------------------------------------------------------
void HatTracker::set_kf_cov(const double qual, cv::Mat &cov)
{
    cv::setIdentity(cov, meas_noise_base_ / qual);
}

//----------------------------------------------------------------------------------
void HatTracker::set_rot_kf_cov(const double cap_qual, const cv::Mat &vel, const cv::Mat &vel_cov, cv::Mat &cov)
{
    // use sigmoid function as partition function
    const double vel_mag = cv::norm(vel);
    const double w = 0.5 / (1.0 + std::exp((vel_mag - 40.0)/20.0)) + 0.5;

    // set covariance from cap detection
    cv::setIdentity(cov);

    if (cap_qual < 0)
        cov.at<double>(0, 0) = 1e2;
    else
        cov.at<double>(0, 0) = rot_meas_noise_base_ * (1.0 / cap_qual) / w;

    // compute covariance from velocity measurement
    if (vel_mag < 10.0) {
        cov.at<double>(1, 1) = 1e2;
    } else {
//        std::cout << "vel: " << vel << "  w: " << w << std::endl;
//        std::cout << "vel cov: " << std::endl;
//        std::cout << vel_cov << std::endl;
        const double &vx = vel.at<double>(0);
        const double &vy = vel.at<double>(1);

        const double den = 1.0 / (vel_mag * vel_mag);
        const double dvx = den * (-vy);
        const double dvy = den * vx;

        const double meas_vel_cov = dvx * dvx * vel_cov.at<double>(0, 0)
                                    + dvy * dvy * vel_cov.at<double>(1, 1);
        cov.at<double>(1, 1) = rot_vel_noise_base_ * meas_vel_cov / (1.0 - w);
    }
}

//----------------------------------------------------------------------------------
void HatTracker::clip_roi(cv::Rect &roi)
{
    roi.x = std::min(roi.x, frame_.cols);
    roi.y = std::min(roi.y, frame_.rows);
    roi.x = std::max(roi.x, 0);
    roi.y = std::max(roi.y, 0);
    roi.width = std::min(roi.width, frame_.cols - roi.x);
    roi.height = std::min(roi.height, frame_.rows - roi.y);
}

}