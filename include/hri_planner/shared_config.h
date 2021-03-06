//----------------------------------------------------------------------------------
//
// Human Robot Interaction Planning Framework
//
// Created on   : 2/25/2018
// Last revision: 2/27/2018
// Author       : Che, Yuhang <yuhangc@stanford.edu>
// Contact      : Che, Yuhang <yuhangc@stanford.edu>
//
//----------------------------------------------------------------------------------

#ifndef HRI_PLANNER_SHARED_CONFIG_H
#define HRI_PLANNER_SHARED_CONFIG_H

#include <string>

#include <ros/ros.h>

#include <json/json.h>

namespace hri_planner {

enum IntentType: int {HumanPriority=0, RobotPriority=1};

class SharedConfig {
public:
    // constructor
    explicit SharedConfig(const std::string& config_file_path="");

    // dimensions of the problem
    int T;
    int nXh;
    int nUh;
    int nXr;
    int nUr;

    // time step
    double dt;

private:
    // to load the configuration
    void load_from_file(const std::string& file_path);
    void load();
};

}

#endif //HRI_PLANNER_SHARED_CONFIG_H
