//----------------------------------------------------------------------------------
//
// Human Robot Interaction Planning Framework
//
// Created on   : 3/7/2018
// Last revision: 3/8/2018
// Author       : Che, Yuhang <yuhangc@stanford.edu>
// Contact      : Che, Yuhang <yuhangc@stanford.edu>
//
//----------------------------------------------------------------------------------

#include "hri_planner/trajectory.h"

namespace hri_planner {

//----------------------------------------------------------------------------------
Trajectory::Trajectory(DynamicsModel dyn_type, int T, double dt): T_(T), dt_(dt), dyn_type(dyn_type)
{
    // create dynamics object
    switch (dyn_type) {
        case CONST_ACC_MODEL:
            dyn_ = std::make_shared<ConstAccDynamics>(dt_);
            nX_ = 4;
            nU_ = 2;
            break;
        case DIFFERENTIAL_MODEL:
            dyn_ = std::make_shared<DifferentialDynamics>(dt_);
            nX_ = 3;
            nU_ = 2;
            break;
        default:
            break;
    }

    nXt_ = T_ * nX_;
    nUt_ = T_ * nU_;

    // initialize x, u, J
    x.setZero(nXt_);
    u.setZero(nUt_);
    Ju.setZero(nXt_, nUt_);
}

//----------------------------------------------------------------------------------
void Trajectory::update(const Eigen::VectorXd& x0_new, const Eigen::VectorXd& u_new)
{
    // update x0 and u
    x0 = x0_new;
    u = u_new;

    compute();
}

//----------------------------------------------------------------------------------
void Trajectory::update(const Eigen::VectorXd &u_new)
{
    // update u
    u = u_new;

    compute();
}

//----------------------------------------------------------------------------------
void Trajectory::compute()
{
    // compute the new trajectory
    Eigen::VectorXd x_last = x0;
    for (int t = 0; t < T_; ++t) {
        dyn_->forward_dyn(x_last, u.segment(t*nU_, nU_), x.segment(t*nX_, nX_));
        x_last = x.segment(t*nX_, nX_);
    }
}

//----------------------------------------------------------------------------------
void Trajectory::compute_jacobian()
{
    for (int t2 = 0; t2 < T_; ++t2) {
        for (int t1 = t2; t1 < T_; ++t1) {
            int xs = (t1-1) * nX_;
            int ys = t2 * nU_;

            if (t1 == t2) {
                if (t1 == 0)
                    dyn_->grad_u(x0, u.segment(ys, nU_), Ju.block(xs+nX_, ys, nX_, nU_));
                else
                    dyn_->grad_u(x.segment(xs, nX_), u.segment(ys, nU_), Ju.block(xs+nX_, ys, nX_, nU_));
            }
            else {
                Eigen::MatrixXd A(nX_, nX_);
                dyn_->grad_x(x.segment(xs, nX_), u.segment(ys, nU_), A);

                Ju.block(xs+nX_, ys, nX_, nU_) = A * Ju.block(xs, ys, nX_, nU_);
            }
        }
    }
}

//----------------------------------------------------------------------------------
Trajectory& Trajectory::operator=(const Trajectory &traj)
{
    // assign the data
    x0 = traj.x0;
    x = traj.x;
    u = traj.u;
    Ju = traj.Ju;

    T_ = traj.T_;
    nX_ = traj.nX_;
    nU_ = traj.nU_;
    nXt_ = traj.nXt_;
    nUt_ = traj.nUt_;
    dt_ = traj.dt_;

    dyn_ = traj.dyn_;
    dyn_type = traj.dyn_type;

    return (*this);
}

} // namespace
