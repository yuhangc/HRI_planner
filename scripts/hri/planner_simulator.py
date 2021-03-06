#!/usr/bin/env python

import numpy as np
import matplotlib.pyplot as plt
import matplotlib.gridspec as gridspec

import rospy
from std_msgs.msg import String
from std_msgs.msg import Float64MultiArray
from geometry_msgs.msg import Twist
from geometry_msgs.msg import PoseWithCovarianceStamped
from geometry_msgs.msg import Quaternion
from nav_msgs.msg import Odometry

import tf

from people_msgs.msg import People
from people_msgs.msg import Person

from hri_planner.msg import PlannedTrajectories

from plotting_utils import *


def wrap_to_pi(ang):
    while ang >= np.pi:
        ang -= 2.0 * np.pi
    while ang < -np.pi:
        ang += 2.0 * np.pi

    return ang


class PlannerSimulator(object):
    def __init__(self):
        # dimensions
        self.T_ = rospy.get_param("~dimension/T", 6)
        self.nXh_ = rospy.get_param("~dimension/nXh", 4)
        self.nUh_ = rospy.get_param("~dimension/nUh", 2)
        self.nXr_ = rospy.get_param("~dimension/nXr", 3)
        self.nUr_ = rospy.get_param("~dimension/nUr", 2)
        self.dt_ = rospy.get_param("~dimension/dt", 0.5)

        # thresholds to simulate detection range
        self.dist_th_detection_ = rospy.get_param("~sensing/dist_th", 4.0)
        self.ang_th_detection_ = rospy.get_param("~sensing/ang_th", 0.6*np.pi)

        # things to publish
        self.xr_ = np.zeros((self.nXr_, ))
        self.ur_ = np.zeros((self.nUr_, ))
        self.xh_ = np.zeros((self.nXh_, ))
        self.uh_ = np.zeros((self.nUh_, ))

        # things to subscribe to
        self.robot_traj_opt_ = None
        self.human_traj_hp_opt_ = None
        self.human_traj_rp_opt_ = None

        self.acomm_ = ""
        self.tcomm_ = -1
        self.robot_intent_ = -1

        self.robot_ctrl_ = np.zeros((self.nUr_, ))

        # "actual" trajectories and initial states
        self.xr0 = np.zeros((self.nXr_, ))
        self.xh0 = np.zeros((self.nXh_, ))
        self.robot_traj = []
        self.human_traj = []

        # stores the plan/prediction
        self.robot_plan = []
        self.human_pred = []

        self.comm_hist = []

        # stores the beliefs and cost history
        self.belief_hist = []
        self.cost_hist = []

        # goals
        self.xr_goal = np.zeros((2, ))
        self.xh_goal = np.zeros((2, ))

        # flags for data update
        self.flag_plan_updated = False
        self.flag_ctrl_updated = False
        self.flag_comm_updated = False

        # tf broadcaster for robot state
        self.robot_state_br = tf.TransformBroadcaster()

        # subscribers and publishers
        self.comm_sub = rospy.Subscriber("/planner/communication", String, self.comm_callback)
        self.ctrl_sub = rospy.Subscriber("/planner/cmd_vel", Twist, self.robot_ctrl_callback)
        self.plan_sub = rospy.Subscriber("/planner/full_plan", PlannedTrajectories, self.plan_callback)
        self.belief_cost_sub = rospy.Subscriber("/planner/belief_and_costs",
                                                Float64MultiArray, self.belief_cost_callback)

        self.robot_state_pub = rospy.Publisher("/amcl_pose", PoseWithCovarianceStamped, queue_size=1)
        self.robot_vel_pub = rospy.Publisher("/odom", Odometry, queue_size=1)
        # self.human_state_pub = rospy.Publisher("/tracked_human", Float64MultiArray, queue_size=1)
        self.human_tracking_pub = rospy.Publisher("/people", People, queue_size=1)
        self.goal_pub = rospy.Publisher("/planner/set_goal", Float64MultiArray, queue_size=1)
        self.planner_ctrl_pub = rospy.Publisher("/planner/ctrl", String, queue_size=1)

    def load_data(self, path, test_id):
        # load initial states, human trajectory, goal states
        init_data = np.loadtxt(path + "/init.txt", delimiter=",")
        goal_data = np.loadtxt(path + "/goal.txt", delimiter=",")
        traj_data = np.loadtxt(path + "/test" + str(test_id) + ".txt", delimiter=",")

        self.xh_goal = goal_data[test_id, 0:2]
        self.xr_goal = goal_data[test_id, 2:5]
        self.xh0 = init_data[test_id, 0:self.nXh_]
        self.xr0 = init_data[test_id, self.nXh_:(self.nXh_+self.nXr_)]

        self.human_traj = traj_data

    def save_data(self, path, test_id):
        traj_data = np.hstack((self.human_traj, self.robot_traj))

        np.savetxt(path + "/block" + str(test_id) + ".txt", traj_data, delimiter=',')

    # main function to run
    def run_simulation(self, robot_intent, Tsim=-1, show_plot=True, generate_plot=True):
        # if not specified, total "simulation" time is length of pre-defined human trajectory
        if Tsim < 0:
            Tsim = len(self.human_traj)

        # create subplots for each frame
        # 4 plots each row
        n_cols = 5
        n_rows = (Tsim - 1) / n_cols + 1

        if generate_plot:
            fig1, axes = plt.subplots(n_rows, n_cols)

        self.xh_ = self.xh0.copy()
        self.xr_ = self.xr0.copy()

        # publish goal first
        goal_data = Float64MultiArray()
        for xr in self.xr_goal:
            goal_data.data.append(xr)
        for xh in self.xh_goal:
            goal_data.data.append(xh)
        for xh in self.xh0[0:2]:
            goal_data.data.append(xh)

        # set intent data
        goal_data.data.append(robot_intent)

        print goal_data.data

        self.goal_pub.publish(goal_data)

        print "robot start position: ", self.xr_
        for t in range(Tsim):
            print "At time step t = ", t*self.dt_

            # publish the current human state
            self.publish_states()

            # tell the planner to start if t = 0
            # otherwise tell the planner to stop pausing
            if t == 0:
                ctrl_data = String()
                ctrl_data.data = "start"
                self.planner_ctrl_pub.publish(ctrl_data)
            else:
                ctrl_data = String()
                ctrl_data.data = "resume"
                self.planner_ctrl_pub.publish(ctrl_data)

            # wait for the planner to finish
            while not (self.flag_plan_updated and self.flag_ctrl_updated):
                if rospy.is_shutdown():
                    return
                rospy.sleep(0.01)

            # reset the flags
            self.flag_plan_updated = False
            self.flag_ctrl_updated = False

            # execute the control and update pose and vel of the robot
            print "executing control: ", self.robot_ctrl_
            self.xr_, self.ur_ = self.robot_dynamics(self.xr_, self.robot_ctrl_)

            # append to full trajectory
            self.robot_traj.append(np.hstack((self.xr_.copy(), self.ur_.copy())))

            # update pose and vel of human
            self.xh_ = self.human_traj[t, 0:self.nXh_]

            # check for communication
            if self.flag_comm_updated:
                print "received communication: ", self.acomm_
                self.flag_comm_updated = False
                self.tcomm_ = t * self.dt_

                self.comm_hist.append(self.acomm_)
            else:
                self.comm_hist.append(-1)

            # save plan/prediction
            self.robot_plan.append(self.robot_traj_opt_.reshape(self.T_, self.nXr_))

            if self.human_traj_hp_opt_.size > 0:
                self.human_pred.append((self.human_traj_hp_opt_.reshape(self.T_, self.nXh_),
                                        self.human_traj_rp_opt_.reshape(self.T_, self.nXh_)))
            else:
                self.human_pred.append((self.human_traj_hp_opt_, self.human_traj_rp_opt_))

            # visualize the plan
            if generate_plot:
                row = t / n_cols
                col = t % n_cols
                self.visualize_frame(axes[row][col], t+1)

        # tell the planner to stop
        ctrl_data = String()
        ctrl_data.data = "stop"
        self.planner_ctrl_pub.publish(ctrl_data)

        # visualize beliefs and partial costs
        if generate_plot:
            fig2, fig3 = self.visualize_belief_and_costs()

            # set tight plot layout
            fig1.tight_layout()
            fig2.tight_layout()
            fig3.tight_layout()

            # show visualization
            if show_plot:
                plt.show()

            return fig1, fig2, fig3

    # helper functions
    def publish_states(self):
        # robot state
        # robot_state = PoseWithCovarianceStamped()
        # robot_state.pose.pose.position.x = self.xr_[0]
        # robot_state.pose.pose.position.y = self.xr_[1]
        # robot_state.pose.pose.orientation.w = np.cos(self.xr_[2] * 0.5)
        # robot_state.pose.pose.orientation.z = np.sin(self.xr_[2] * 0.5)
        #
        # self.robot_state_pub.publish(robot_state)

        self.robot_state_br.sendTransform((self.xr_[0], self.xr_[1], 0.0),
                                          tf.transformations.quaternion_from_euler(0, 0, self.xr_[2]),
                                          rospy.Time.now(),
                                          "/base_footprint",
                                          "/map")

        # robot vel
        odom_data = Odometry()
        odom_data.twist.twist.linear.x = self.ur_[0]
        odom_data.twist.twist.angular.z = self.ur_[1]

        self.robot_vel_pub.publish(odom_data)

        # human state
        # check if human is within detection range
        # x_diff = self.xh_[0:2] - self.xr_[0:2]
        # if np.linalg.norm(x_diff) > self.dist_th_detection_:
        #     return
        #
        # th_rel = wrap_to_pi(np.arctan2(x_diff[1], x_diff[0]) - self.xr_[2])
        # if np.abs(th_rel) > self.ang_th_detection_:
        #     return

        people_states = People()
        person_state = Person()

        person_state.position.x = self.xh_[0]
        person_state.position.y = self.xh_[1]
        person_state.velocity.x = self.xh_[2]
        person_state.velocity.y = self.xh_[3]

        people_states.people.append(person_state)

        self.human_tracking_pub.publish(people_states)

    # visualize the "frame"
    def visualize_frame(self, ax, t):
        # plot previous trajectories
        robot_traj = np.asarray(self.robot_traj)[0:t, 0:self.nXr_].reshape(t, self.nXr_)
        human_traj = np.asarray(self.human_traj)[0:t, 0:self.nXh_].reshape(t, self.nXh_)

        ax.plot(robot_traj[:, 0], robot_traj[:, 1], "-o",
                color=(0.3, 0.3, 0.9), fillstyle="none", lw=1.5, label="robot_traj")
        ax.plot(human_traj[:, 0], human_traj[:, 1], "-o",
                color=(0.1, 0.1, 0.1), fillstyle="none", lw=1.5, label="human_traj")

        # plot the plan
        robot_plan = self.robot_traj_opt_.reshape(self.T_, self.nXr_)
        ax.plot(robot_plan[:, 0], robot_plan[:, 1], "-",
                color=(0.3, 0.3, 0.9, 0.5), lw=1.0, label="robot_plan")

        if self.human_traj_hp_opt_.size > 0:
            human_plan_hp = self.human_traj_hp_opt_.reshape(self.T_, self.nXh_)
            ax.plot(human_plan_hp[:, 0], human_plan_hp[:, 1], "-",
                    color=(0.1, 0.1, 0.1, 0.5), lw=1.0, label="human_pred_hp")

        if self.human_traj_rp_opt_.size > 0:
            human_plan_rp = self.human_traj_rp_opt_.reshape(self.T_, self.nXh_)
            ax.plot(human_plan_rp[:, 0], human_plan_rp[:, 1], "--",
                    color=(0.1, 0.1, 0.1, 0.5), lw=1.0, label="human_pred_rp")

        # plot the goals
        ax.plot(self.xr_goal[0], self.xr_goal[1], 'ob')
        ax.plot(self.xh_goal[0], self.xh_goal[1], 'ok')

        ax.axis("equal")
        ax.set_title("t = " + str(t))

    # visualize belief changes and partial costs
    def visualize_belief_and_costs(self):
        if not self.belief_hist:
            return

        beliefs = np.asarray(self.belief_hist)
        costs = np.asarray(self.cost_hist)

        fig1, ax = plt.subplots(2, 1)
        ax[0].plot(beliefs, '-ks', lw=1.5)
        ax[0].set_title("belief of human having priority")

        ax[1].plot(costs[:, 0], '-bs', lw=1.5, fillstyle="none", label="cost no communication")
        ax[1].plot(costs[:, 1], '--b^', lw=1.5, fillstyle="none", label="cost communication")
        ax[1].set_title("robot intent is " + str(self.robot_intent_))
        ax[1].legend()

        # plot for partial costs
        fig2, ax = plt.subplots(2, 1)
        ax[0].plot(costs[:, 2], '-bs', lw=1.5, fillstyle="none", label="cost hp")
        ax[0].plot(costs[:, 3], '--b^', lw=1.5, fillstyle="none", label="cost rp")
        ax[0].legend()
        ax[0].set_title("no communication")

        ax[1].plot(costs[:, 4], '-bs', lw=1.5, fillstyle="none", label="cost hp")
        ax[1].plot(costs[:, 5], '--b^', lw=1.5, fillstyle="none", label="cost rp")
        ax[1].legend()
        ax[1].set_title("with communication")

        return fig1, fig2

    # robot dynamics
    def robot_dynamics(self, x, u):
        # get parameters
        v_max = rospy.get_param("~robot_dynamics/v_max", [0.50, 3.0])
        a_max = rospy.get_param("~robot_dynamics/a_max", [0.5, 2.0])
        v_std = rospy.get_param("~robot_dynamics/v_std", [0.0, 0.0])

        v_max = np.asarray(v_max)
        a_max = np.asarray(a_max)
        v_std = np.asarray(v_std)

        # try to reach the commanded velocity
        u = np.clip(u, -v_max, v_max)
        u = np.clip(u, self.ur_ - a_max * self.dt_, self.ur_ + a_max * self.dt_)

        # sample velocity noise
        if v_std[0] > 0:
            dv = np.random.normal(0.0, v_std[0], 1)
        else:
            dv = 0.0

        if v_std[1] > 0:
            dom = np.random.normal(0.0, v_std[1], 1)
        else:
            dom = 0.0

        u += np.array([dv, dom])

        # update position
        th = x[2]
        th_new = th + u[1] * self.dt_

        if np.abs(u[1]) > 1e-3:
            R = u[0] / u[1]
            x[0] += R * (np.sin(th_new) - np.sin(th))
            x[1] -= R * (np.cos(th_new) - np.cos(th))
        else:
            x[0] += u[0] * np.cos(th) * self.dt_
            x[1] += u[0] * np.sin(th) * self.dt_

        x[2] = th_new

        return x, u

    # run multiple simulations
    def run_tests(self, data_path, human_traj_id, test_id):
        # load the human trajectory/goals
        self.load_data(data_path, human_traj_id)

        # load robot initial conditions and goals
        test_path = data_path + "/test_config" + str(test_id)
        xr_goals = np.loadtxt(test_path + "/goal.txt", delimiter=',')
        xr_inits = np.loadtxt(test_path + "/init.txt", delimiter=',')

        if xr_goals.ndim == 1:
            xr_goals = xr_goals.reshape(1, xr_goals.shape[0])
        print xr_goals

        if xr_inits.ndim == 1:
            xr_inits = xr_inits.reshape(1, xr_inits.shape[0])

        # show the initial positions and goals
        plt.plot(xr_inits[:, 0], xr_inits[:, 1], 'bo')
        plt.plot(xr_goals[:, 0], xr_goals[:, 1], 'ro')
        plt.show()

        # to save the communication
        comm_actions_hp = np.zeros((len(xr_inits), len(xr_goals)), dtype=int)
        tcomm_hp = np.zeros((len(xr_inits), len(xr_goals)), dtype=int)
        comm_actions_rp = np.zeros((len(xr_inits), len(xr_goals)), dtype=int)
        tcomm_rp = np.zeros((len(xr_inits), len(xr_goals)), dtype=int)

        # run simulation with all combinations of xr_init and xr_goal
        for (i, xr_init) in enumerate(xr_inits):
            for (j, xr_goal) in enumerate(xr_goals):
                # run simulation and save data
                fig_names = ["traj_hist", "belief_cost", "partial_costs"]

                self.clear_hist(xr_init, xr_goal)
                fig1, fig2, fig3 = self.run_simulation(0, Tsim=15, show_plot=False)
                self.save_test_data(test_path, [fig1, fig2, fig3], fig_names, i, j, 0)

                tcomm_hp[i, j] = self.tcomm_
                if self.tcomm_ >= 0:
                    comm_actions_hp[i, j] = 0
                else:
                    comm_actions_hp[i, j] = -1

                self.clear_hist(xr_init, xr_goal)
                fig1, fig2, fig3 = self.run_simulation(1, Tsim=15, show_plot=False)
                self.save_test_data(test_path, [fig1, fig2, fig3], fig_names, i, j, 1)

                tcomm_rp[i, j] = self.tcomm_
                if self.tcomm_ >= 0:
                    comm_actions_rp[i, j] = 1
                else:
                    comm_actions_rp[i, j] = -1

        # save the communication actions
        np.savetxt(test_path + "/data/comm_actions_hp.txt", comm_actions_hp, delimiter=',', fmt="%d")
        np.savetxt(test_path + "/data/comm_actions_rp.txt", comm_actions_rp, delimiter=',', fmt="%d")
        np.savetxt(test_path + "/data/tcomm_hp.txt", tcomm_hp, delimiter=',', fmt="%1.2f")
        np.savetxt(test_path + "/data/tcomm_rp.txt", tcomm_rp, delimiter=',', fmt="%1.2f")

    # run multiple simulations
    def run_single_test(self, data_path, human_traj_id, test_id, intent, test_init_id=0, test_goal_id=0):
        # load the human trajectory/goals
        self.load_data(data_path, human_traj_id)

        # load robot initial conditions and goals
        test_path = data_path + "/test_config" + str(test_id)
        xr_goals = np.loadtxt(test_path + "/goal.txt", delimiter=',')
        xr_inits = np.loadtxt(test_path + "/init.txt", delimiter=',')

        if xr_goals.ndim == 1:
            xr_goals = xr_goals.reshape(1, xr_goals.shape[0])
        print xr_goals

        if xr_inits.ndim == 1:
            xr_inits = xr_inits.reshape(1, xr_inits.shape[0])

        self.clear_hist(xr_inits[test_init_id], xr_goals[test_goal_id])
        self.run_simulation(intent, show_plot=False, generate_plot=True)

        self.visualize_test_result([2, 3, 6, 10, 14])
        plt.show()

    def visualize_test_result(self, t_plot):
        fig = plt.figure(figsize=(9.6, 4.2))
        nplots = len(t_plot)

        gs = gridspec.GridSpec(2, nplots, height_ratios=[1.2, 1])
        traj_ax = [plt.subplot(gs[0, i]) for i in range(nplots)]
        vel_ax = plt.subplot(gs[1, :])

        self.robot_traj = np.asarray(self.robot_traj)
        self.human_traj = np.asarray(self.human_traj)

        for ax, t in zip(traj_ax, t_plot):
            robot_traj = np.asarray(self.robot_traj)[0:t, 0:self.nXr_].reshape(t, self.nXr_)
            human_traj = np.asarray(self.human_traj)[0:t, 0:self.nXh_].reshape(t, self.nXh_)

            rj = ax.plot(robot_traj[:, 0], robot_traj[:, 1], "-",
                         color=(1.0, 0.3, 0.3), fillstyle="none", lw=1.5, label="robot_traj")
            hj = ax.plot(human_traj[:, 0], human_traj[:, 1], "-",
                         color=(0.0, 0.0, 0.0), fillstyle="none", lw=1.5, label="human_traj")

            add_arrow(rj[0], position=robot_traj[t-2, 0], size=12)
            add_arrow(hj[0], position=human_traj[t-2, 0], size=12)

            # plot the plan
            robot_plan = self.robot_plan[t]
            robot_plan = np.vstack((robot_traj[(t-1):t, :], robot_plan))
            ax.plot(robot_plan[:, 0], robot_plan[:, 1], "-",
                    color=(0.9, 0.3, 0.3, 0.5), lw=1.0, label="robot_plan")

            human_plan_hp, human_plan_rp = self.human_pred[t]
            if self.human_traj_hp_opt_.size > 0:
                human_plan_hp = np.vstack((human_traj[(t-1):t, 0:self.nXh_], human_plan_hp))
                human_plan_rp = np.vstack((human_traj[(t-1):t, 0:self.nXh_], human_plan_rp))
                ax.plot(human_plan_hp[:, 0], human_plan_hp[:, 1], "-",
                        color=(0.1, 0.1, 0.1, 0.5), lw=1.0, label="human_pred_hp")

            if self.human_traj_rp_opt_.size > 0:
                ax.plot(human_plan_rp[:, 0], human_plan_rp[:, 1], "--",
                        color=(0.1, 0.1, 0.1, 0.5), lw=1.0, label="human_pred_rp")

            # plot the goals
            ax.plot(self.xr_goal[0], self.xr_goal[1], 'or', fillstyle="none")

            ax.axis("equal")
            ax.axis([-0.5, 5, 0, 7])

            turn_off_axes_labels(ax)

            # place a text box in bottom left in axes coords
            props = dict(boxstyle='square', facecolor='white')
            ax.text(0.05, 0.04, "t="+str(t*0.5)+"s", transform=ax.transAxes, fontsize=14,
                    verticalalignment='bottom', bbox=props)

        # plot the velocity profile
        vr = self.robot_traj[:, self.nXr_]
        vh = np.linalg.norm(self.human_traj[:, 2:4], axis=1)
        tv = np.arange(0, len(vr)) * 0.5

        vel_ax.plot(tv, vr, '--k', lw=2, label="robot")
        vel_ax.plot(tv, vh, '-k', lw=2, label="human")
        vel_ax.set_yticks(np.arange(0, 1.21, 0.4))

        # highlight the communication region
        for tt in t_plot:
            if self.comm_hist[tt-1] > 0:
                color = (0.8, 0.5, 0.5)
            else:
                color = (0.5, 0.5, 0.7)
            vel_ax.axvspan(tv[tt]-0.05, tv[tt]+0.05, facecolor=color, edgecolor="none", alpha=0.5)

        vel_ax.legend()

        # fig.tight_layout()
        plt.show()

    def clear_hist(self, x_init, x_goal):
        # trajectory
        self.robot_traj = []

        # stores the beliefs and cost history
        self.belief_hist = []
        self.cost_hist = []

        # plan/predictions/comm
        self.robot_plan = []
        self.human_pred = []
        self.comm_hist = []

        # flags for data update
        self.flag_plan_updated = False
        self.flag_ctrl_updated = False
        self.flag_comm_updated = False

        self.acomm_ = ""
        self.tcomm_ = -1

        # set the initial conditions
        self.xr0 = x_init.copy()
        self.xr_goal = x_goal.copy()

    def save_test_data(self, save_path, figs, fig_names, i, j, intent):
        # save the figures
        intent_str = ["hp", "rp"]
        for test_fig, fig_name in zip(figs, fig_names):
            test_fig.savefig(save_path + "/figs/" + fig_name + "_" + str(i) +
                             "_" + str(j) + "_" + intent_str[intent] + ".pdf")

            # close the figure afterwards
            plt.close(test_fig)

        # save the trajectories
        Tsim = len(self.robot_traj)
        self.robot_traj = np.asarray(self.robot_traj)
        self.belief_hist = np.asarray(self.belief_hist)[:, None]
        self.cost_hist = np.asarray(self.cost_hist)
        np.savetxt(save_path + "/data/traj_belief" + "_" + str(i) + "_" + str(j) + "_" + intent_str[intent],
                   np.hstack((self.robot_traj, self.human_traj[0:Tsim], self.belief_hist, self.cost_hist)),
                   delimiter=',')

    # callbacks
    def comm_callback(self, comm_msg):
        if comm_msg.data == "Attract":
            self.acomm_ = "HumanPriority"
        else:
            self.acomm_ = "RobotPriority"

        self.flag_comm_updated = True
        print "[callback] received communication: ", self.acomm_

    def robot_ctrl_callback(self, ctrl_msg):
        self.robot_ctrl_[0] = ctrl_msg.linear.x
        self.robot_ctrl_[1] = ctrl_msg.angular.z
        self.flag_ctrl_updated = True

    def plan_callback(self, plan_msg):
        self.robot_traj_opt_ = np.asarray(plan_msg.robot_traj_opt)
        self.human_traj_hp_opt_ = np.asarray(plan_msg.human_traj_hp_opt)
        self.human_traj_rp_opt_ = np.asarray(plan_msg.human_traj_rp_opt)
        self.flag_plan_updated = True

        if not plan_msg.human_traj_hp_opt:
            # add to belief and cost history
            self.belief_hist.append(0)
            self.cost_hist.append(np.zeros((6, )))

    def belief_cost_callback(self, msg):
        self.belief_hist.append(msg.data[0])
        self.cost_hist.append(msg.data[1:7])


if __name__ == "__main__":
    rospy.init_node("planner_simulator")

    simulator = PlannerSimulator()
    # simulator.load_data("/home/yuhang/Documents/hri_log/test_data", 0)
    # simulator.run_simulation(0)
    # simulator.save_data("/home/yuhang/Documents/hri_log/test_data", 0)

    # simulator.run_tests("/home/yuhang/Documents/hri_log/test_data", 0, 9)
    simulator.run_single_test("/home/yuhang/Documents/hri_log/test_data", 0, 5, 0, test_init_id=103)
