#include "../include/WalkerEnv.hpp"

thread_local std::mt19937 WalkerEnv::gen_;

WalkerEnv::WalkerEnv(const std::string &resourceDir, bool visualizable)
    : resourceDir_(std::move(resourceDir)), visualizable_(visualizable), normDist_(0, 1)
{
    createWorldAndRobot();
    initializeContainers();
    setPdGains();
    initializeObservationSpace();
    if (visualizable_)
        initializeVisualization();
}

WalkerEnv::~WalkerEnv()
{
    if (server_)
        server_->killServer();
}

void WalkerEnv::createWorldAndRobot()
{
    world_ = std::make_unique<raisim::World>();
    robot_ = world_->addArticulatedSystem(resourceDir_);
    robot_->setName("robot");
    robot_->setControlMode(raisim::ControlMode::PD_PLUS_FEEDFORWARD_TORQUE);
    world_->addGround();
    gcDim_ = robot_->getGeneralizedCoordinateDim();
    gvDim_ = robot_->getDOF();
    nJoints_ = gvDim_ - 6;
}

void WalkerEnv::initializeContainers()
{
    gc_.setZero(gcDim_);
    gcInit_.setZero(gcDim_);
    gv_.setZero(gvDim_);
    gvInit_.setZero(gvDim_);
    pTarget_.setZero(gcDim_);
    vTarget_.setZero(gvDim_);

    gcInit_ << 0, 0, 0.50, 1.0, 0.0, 0.0, 0.0, 0.03, 0.4, -0.8, -0.03, 0.4, -0.8, 0.03, -0.4, 0.8, -0.03, -0.4, 0.8;
}

void WalkerEnv::setPdGains()
{
    Eigen::VectorXd jointPgain(gvDim_), jointDgain(gvDim_);
    jointPgain.setZero();
    jointPgain.tail(nJoints_).setConstant(50.0);
    jointDgain.setZero();
    jointDgain.tail(nJoints_).setConstant(0.2);
    robot_->setPdGains(jointPgain, jointDgain);
    robot_->setGeneralizedForce(Eigen::VectorXd::Zero(gvDim_));
}

void WalkerEnv::initializeObservationSpace()
{
    obDim_ = 1 + 3 + 3 + 3 + nJoints_ * 2;
    actionDim_ = nJoints_;
    actionMean_.setZero(actionDim_);
    actionStd_.setZero(actionDim_);
    obDouble_.setZero(obDim_);
    actionMean_ = gcInit_.tail(nJoints_);
    double action_std = 0.3;
    actionStd_.setConstant(action_std);
    footIndices_.insert(robot_->getBodyIdx("LF_SHANK"));
    footIndices_.insert(robot_->getBodyIdx("RF_SHANK"));
    footIndices_.insert(robot_->getBodyIdx("LH_SHANK"));
    footIndices_.insert(robot_->getBodyIdx("RH_SHANK"));
}

void WalkerEnv::initializeVisualization()
{
    std::cout << "Starting visualization thread..." << std::endl;
    server_ = std::make_unique<raisim::RaisimServer>(world_.get());
    server_->launchServer();
    server_->focusOn(robot_);
}

void WalkerEnv::setInitConstants(Eigen::VectorXd gcInit, Eigen::VectorXd gvInit, Eigen::VectorXd actionMean, Eigen::VectorXd actionStd, Eigen::VectorXd pGain, Eigen::VectorXd dGain)
{
    assert(gcInit.size() == gcDim_);
    assert(gvInit.size() == gvDim_);
    assert(actionMean.size() == actionDim_);
    assert(actionStd.size() == actionDim_);
    assert(pGain.size() == gvDim_);
    assert(dGain.size() == gvDim_);

    gcInit_ = gcInit;
    gvInit_ = gvInit;

    actionMean_ = actionMean;
    actionStd_ = actionStd;

    robot_->setPdGains(pGain, dGain);
}

void WalkerEnv::init()
{
    robot_->setState(gcInit_, gvInit_);
    updateObservation();
}

void WalkerEnv::reset()
{
    robot_->setState(gcInit_, gvInit_);
    updateObservation();
}

float WalkerEnv::step(const Eigen::Ref<EigenVec> &action)
{

    Eigen::VectorXd pTargetTail = action.cast<double>();
    pTargetTail = pTargetTail.cwiseProduct(actionStd_);
    pTargetTail += actionMean_;
    pTarget_.tail(nJoints_) = pTargetTail;

    robot_->setPdTarget(pTarget_, vTarget_);

    for (int i = 0; i < int(control_dt_ / simulation_dt_ + 1e-10); i++)
    {
        if (server_)
            server_->lockVisualizationServerMutex();
        world_->integrate();
        if (server_)
            server_->unlockVisualizationServerMutex();
    }
    updateObservation();

    float re = -4e-5 * robot_->getGeneralizedForce().squaredNorm() +
               0.3 * std::min(4.0, bodyLinearVel_[0]);

    return re;
}

void WalkerEnv::updateObservation()
{
    robot_->getState(gc_, gv_);
    raisim::Vec<4> quat;
    raisim::Mat<3, 3> rot;
    quat[0] = gc_[3];
    quat[1] = gc_[4];
    quat[2] = gc_[5];
    quat[3] = gc_[6];
    raisim::quatToRotMat(quat, rot);
    bodyLinearVel_ = rot.e().transpose() * gv_.segment(0, 3);
    bodyAngularVel_ = rot.e().transpose() * gv_.segment(3, 3);

    obDouble_ << gc_[2],                 /// body height
        rot.e().row(2).transpose(),      /// body orientation
        gc_.tail(nJoints_),              /// joint angles
        bodyLinearVel_, bodyAngularVel_, /// body linear&angular velocity
        gv_.tail(nJoints_);              /// joint velocity
}

void WalkerEnv::observe(Eigen::Ref<EigenVec> ob)
{
    ob = obDouble_.cast<float>();
}

bool WalkerEnv::isTerminalState(float &terminalReward)
{
    terminalReward = float(terminalRewardCoeff_);

    /// if the contact body is not feet
    for (auto &contact : robot_->getContacts())
        if (footIndices_.find(contact.getlocalBodyIndex()) == footIndices_.end())
            return true;

    terminalReward = 0.f;
    return false;
}

void WalkerEnv::curriculumUpdate() {}

void WalkerEnv::setSimulationTimeStep(double dt)
{
    simulation_dt_ = dt;
    world_->setTimeStep(dt);
}

void WalkerEnv::close() {}

void WalkerEnv::setSeed(int seed) {}

void WalkerEnv::setControlTimeStep(double dt)
{
    control_dt_ = dt;
}

int WalkerEnv::getObDim()
{
    return obDim_;
}

int WalkerEnv::getActionDim()
{
    return actionDim_;
}

double WalkerEnv::getControlTimeStep()
{
    return control_dt_;
}

double WalkerEnv::getSimulationTimeStep()
{
    return simulation_dt_;
}

raisim::World *WalkerEnv::getWorld()
{
    return world_.get();
}

void WalkerEnv::turnOffVisualization()
{
    server_->hibernate();
}

void WalkerEnv::turnOnVisualization()
{
    server_->wakeup();
}

void WalkerEnv::startRecordingVideo(const std::string &videoName)
{
    server_->startRecordingVideo(videoName);
}

void WalkerEnv::stopRecordingVideo()
{
    server_->stopRecordingVideo();
}
