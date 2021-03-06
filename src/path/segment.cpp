// This file is a part of the traxxs framework.
// Copyright 2018, AKEOLAB S.A.S.
// Main contributor(s): Aurelien Ibanez, aurelien@akeo-lab.com
// 
// This software is a computer program whose purpose is to help create and manage trajectories.
// 
// This software is governed by the CeCILL-C license under French law and
// abiding by the rules of distribution of free software.  You can  use, 
// modify and/ or redistribute the software under the terms of the CeCILL-C
// license as circulated by CEA, CNRS and INRIA at the following URL
// "http://www.cecill.info". 
// 
// As a counterpart to the access to the source code and  rights to copy,
// modify and redistribute granted by the license, users are provided only
// with a limited warranty  and the software's author,  the holder of the
// economic rights,  and the successive licensors  have only  limited
// liability. 
// 
// In this respect, the user's attention is drawn to the risks associated
// with loading,  using,  modifying and/or developing or reproducing the
// software by the user in light of its specific status of free software,
// that may mean  that it is complicated to manipulate,  and  that  also
// therefore means  that it is reserved for developers  and  experienced
// professionals having in-depth computer knowledge. Users are therefore
// encouraged to load and test the software's suitability as regards their
// requirements in conditions enabling the security of their systems and/or 
// data to be ensured and,  more generally, to use and operate it in the 
// same conditions as regards security. 
// 
// The fact that you are presently reading this means that you have had
// knowledge of the CeCILL-C license and that you accept its terms.

#include "traxxs/path/segment.hpp"

Eigen::VectorXd traxxs::path::normalizedOrZero( const Eigen::VectorXd& vec, double norm_tolerance /*= kZero*/ )
{
  double norm = vec.norm();
  if ( norm < norm_tolerance )
    return Eigen::VectorXd::Zero( vec.size() );
  return vec.normalized() ; // let Eigen handle the rest (NaNs, etc.)
}

Eigen::VectorXd traxxs::path::stack( const Eigen::VectorXd& vec_top, const Eigen::VectorXd& vec_bottom )
{
  Eigen::VectorXd vstack = Eigen::VectorXd( vec_top.rows() + vec_bottom.rows() );
  vstack.segment( 0, vec_top.rows() )             = vec_top;
  vstack.segment( vec_top.rows(), vec_bottom.rows() ) = vec_bottom;
  return vstack;
}

bool traxxs::path::PathSegment::do_init()
{
  if ( path_bounds_.dx.size() <= 0 || path_bounds_.ddx.size() <= 0 || path_bounds_.j.size() <= 0 )
    throw std::invalid_argument( "Bounds should have been initialized with correct size in PathSegment." );
  
  // project PathConditions onto the path to set the PathCondition.arcConditions
  /** \todo same for dds and j ? */
  arc::ArcConditions arcconds_start = cond_start_.getArcConditions();
  arc::ArcConditions arcconds_end = cond_end_.getArcConditions();
  double norm;
  if ( cond_start_.pathConditions.dx.size() > 0 && !cond_start_.pathConditions.dx.hasNaN() ) { // we have a valid desired dx condition
    norm = this->getDerivative( 1, 0.0 ).norm();
    if ( norm > constants::kZero ) // we have a not null f'
      arcconds_start.ds = cond_start_.pathConditions.dx.dot( this->getDerivative( 1, 0.0 ) ) / (norm*norm);
  }
  if ( cond_end_.pathConditions.dx.size() > 0 && !cond_end_.pathConditions.dx.hasNaN() ) { // we have a valid desired dx condition
    norm = this->getDerivative( 1, this->getLength() ).norm();
    if ( norm > constants::kZero ) // we have a not null f'
      arcconds_end.ds = cond_end_.pathConditions.dx.dot( this->getDerivative( 1, 0.0 ) ) / (norm*norm);
  }
  
  arcconds_start.s = 0;
  arcconds_end.s = this->getLength();
  
  cond_start_.setArcConditions( arcconds_start );
  cond_end_.setArcConditions( arcconds_end );
  
  return true;
}

traxxs::path::StackedSegments::StackedSegments(const std::vector<std::shared_ptr<PathSegment> >& segments)
{
  this->setSegments( segments );
}

traxxs::path::StackedSegments::StackedSegments( std::shared_ptr< PathSegment >& segment_top, std::shared_ptr< PathSegment >& segment_bottom )
    : StackedSegments( std::vector<std::shared_ptr<PathSegment> >{segment_top, segment_bottom} )
{}

bool traxxs::path::StackedSegments::do_init()
{
  // we do not call PathSegment::do_init() on purpose, since StackedSegments is a wrapper
  this->length_ = 1.0; // unit arc variable used for StackedSegments
  
  this->path_bounds_ = PathBounds(0); // reset to zero
  
  // we have to reconstruct the path_bounds
  bool ret = true;
  for ( const auto& seg : segments_ ) {
    ret &= seg->init(); /** \todo should we check if already initialized ? Here we force it no matter what. Probably safer */
    this->path_bounds_.dx = stack( this->path_bounds_.dx, seg->getPathBounds().dx );
    this->path_bounds_.ddx = stack( this->path_bounds_.ddx, seg->getPathBounds().ddx );
    this->path_bounds_.j = stack( this->path_bounds_.j, seg->getPathBounds().j );
  }
  // we have to reconstruct the start/end path conditions
  
  this->cond_start_.x = this->getConfiguration( 0.0 ); // use the newly implemented interface
  this->cond_end_.x = this->getConfiguration( this->length_ );
  
  this->cond_start_.pathConditions.dx = Eigen::VectorXd();
  for ( const auto& seg : segments_ )
    this->cond_start_.pathConditions.dx = stack( this->cond_start_.pathConditions.dx, seg->cond_start_.pathConditions.dx );
  this->cond_end_.pathConditions.dx = Eigen::VectorXd();
  for ( const auto& seg : segments_ )
    this->cond_end_.pathConditions.dx = stack( this->cond_end_.pathConditions.dx, seg->cond_end_.pathConditions.dx );
  
  // now adapt start/end arc conditions
  arc::ArcConditions arcconds_start = cond_start_.getArcConditions();
  arc::ArcConditions arcconds_end = cond_end_.getArcConditions();
  
  double norm;
  if ( cond_start_.pathConditions.dx.size() > 0 && !cond_start_.pathConditions.dx.hasNaN() ) { // we have a valid desired dx condition
    norm = this->getDerivative( 1, 0.0 ).norm();
    if ( norm > constants::kZero ) // we have a not null f'
      arcconds_start.ds = cond_start_.pathConditions.dx.dot( this->getDerivative( 1, 0.0 ) ) / (norm*norm);
  }
  if ( cond_end_.pathConditions.dx.size() > 0 && !cond_end_.pathConditions.dx.hasNaN() ) { // we have a valid desired dx condition
    norm = this->getDerivative( 1, this->getLength() ).norm();
    if ( norm > constants::kZero ) // we have a not null f'
      arcconds_end.ds = cond_end_.pathConditions.dx.dot( this->getDerivative( 1, 0.0 ) ) / (norm*norm);
  }
  
  arcconds_start.s = 0;
  arcconds_end.s = this->getLength();
  
  cond_start_.setArcConditions( arcconds_start );
  cond_end_.setArcConditions( arcconds_end );
  
  return ret;
}

Eigen::VectorXd traxxs::path::StackedSegments::do_get_derivative(unsigned int order, double s) const
{
  Eigen::VectorXd der;
  double L, mult;
  for ( const auto& seg : segments_ ) {
    L = seg->getLength();
    mult = std::pow( L, order );
    der = stack( der, mult * seg->getDerivative( order, s * L ) );
  }
  return der;
}

Eigen::VectorXd traxxs::path::StackedSegments::do_get_derivative_cwise_abs_max(unsigned int order) const
{
  Eigen::VectorXd der;
  double L, mult;
  for ( const auto& seg : segments_ ) {
    L = seg->getLength();
    mult = std::pow( L, order );
    der = stack( der, mult * seg->getDerivativeCwiseAbsMax( order ) );
  }
  return der;
}


traxxs::path::LinearSegment::LinearSegment( const PathWaypoint& start, const PathWaypoint& end, const PathBounds& bounds )
    : Cloneable< PathSegment, LinearSegment >( start, end, bounds )
{}

bool traxxs::path::LinearSegment::do_init()
{
  this->length_ = (cond_end_.x - cond_start_.x).norm();
  bool ret = PathSegment::do_init();
  return ret;
}


Eigen::VectorXd traxxs::path::LinearSegment::do_get_derivative( unsigned int order, double s ) const
{
  switch ( order ) {
    case 0: {
      return this->get_derivative_0( s );
      break;
    }
    case 1: {
      return this->get_derivative_1( s );
      break;
    }
    default: {
      return Eigen::VectorXd::Zero( this->cond_start_.x.size() );
      break;
    }
  }
}

Eigen::VectorXd traxxs::path::LinearSegment::do_get_derivative_cwise_abs_max( unsigned int order ) const
{
  switch ( order ) {
    case 1: {
      return this->get_derivative_1( 0.0 ).cwiseAbs(); // is constant over s
      break;
    }
    default: {
      return Eigen::VectorXd::Zero( this->cond_start_.x.size() );
      break;
    }
  }
}

Eigen::VectorXd traxxs::path::LinearSegment::get_derivative_0( double s ) const
{
  if ( this->getLength() < constants::kZero )
    return this->cond_start_.x;
  
  s /= this->getLength();
  return ( 1.0 - s ) * this->cond_start_.x + s * this->cond_end_.x;
}

Eigen::VectorXd traxxs::path::LinearSegment::get_derivative_1( double s ) const
{
  (void) s; // for linear segments, the tangent (1st) is constant, so arc length is unused
  /** \fixme what to do when length zero ? */
  if ( this->getLength() < constants::kZero )
    return Eigen::VectorXd::Ones( this->cond_start_.x.size() );
  
  return ( this->cond_end_.x - this->cond_start_.x ) / this->getLength();
}


traxxs::path::SmoothStep7::SmoothStep7( const PathWaypoint& start, const PathWaypoint& end, const PathBounds& bounds )
    : Cloneable< PathSegment, SmoothStep7 >( start, end, bounds )
{}

bool traxxs::path::SmoothStep7::do_init() 
{
  this->length_ = 1.0; // unit parameter for SmoothStep7
  return PathSegment::do_init();
}

Eigen::VectorXd traxxs::path::SmoothStep7::do_get_derivative( unsigned int order, double s ) const
{
  switch ( order ) {
    case 0: {
      return this->get_derivative_0( s );
      break;
    }
    case 1: {
      return this->get_derivative_1( s );
      break;
    }
    case 2: {
      return this->get_derivative_2( s );
      break;
    }
    case 3: {
      return this->get_derivative_3( s );
      break;
    }
    default: {
      throw std::runtime_error("Derivatives above 3 are not implemented for SmoothStep7.");
      break;
    }
  }
}

Eigen::VectorXd traxxs::path::SmoothStep7::do_get_derivative_cwise_abs_max( unsigned int order ) const
{
  switch ( order ) {
    case 1: {
      return this->getDerivative( order, 1.0/2.0 ).cwiseAbs();
      break;
    }
    case 2: {
      return this->getDerivative( order, (5.0 + std::sqrt(5.0))/10.0 ).cwiseAbs();
      break;
    }
    case 3: {
      return this->getDerivative( order, 1.0/2.0 ).cwiseAbs();
      break;
    }
    default: {
      throw std::runtime_error("Derivatives above 3 are not implemented for SmoothStep7.");
      break;
    }
  }
}

Eigen::VectorXd traxxs::path::SmoothStep7::get_derivative_0( double s ) const
{
  s = std::min( std::max( s, 0.0 ), 1.0 ); // clamp
  double s_4 = s * s * s * s;
  double f = s_4 * ( -20*s*s*s + 70*s*s - 84*s + 35 );
  return (1.0 - f) * this->cond_start_.x + f * this->cond_end_.x;
}

Eigen::VectorXd traxxs::path::SmoothStep7::get_derivative_1( double s ) const
{
  s = std::min( std::max( s, 0.0 ), 1.0 ); // clamp
  double s_3 = s * s * s;
  double fp = s_3 * ( -140*s*s*s + 420*s*s - 420*s + 140 );
  return -fp * this->cond_start_.x + fp * this->cond_end_.x;
}

Eigen::VectorXd traxxs::path::SmoothStep7::get_derivative_2( double s ) const
{
  s = std::min( std::max( s, 0.0 ), 1.0 ); // clamp
  double s_2 = s * s;
  double fpp = s_2 * ( -840*s*s*s + 2100*s*s - 1680*s + 420 );
  return -fpp * this->cond_start_.x + fpp * this->cond_end_.x;
}

Eigen::VectorXd traxxs::path::SmoothStep7::get_derivative_3( double s ) const
{
  s = std::min( std::max( s, 0.0 ), 1.0 ); // clamp
  double fppp = s * ( -4200*s*s*s + 8400*s*s - 5040*s + 840 );
  return -fppp * this->cond_start_.x + fppp * this->cond_end_.x;
}


traxxs::path::CircularBlend::CircularBlend( const PathWaypoint& start, const PathWaypoint& end, const PathBounds& bounds, 
                              const Eigen::VectorXd& waypoint, 
                              double maxDeviation )
    : Cloneable< BlendSegment< double >, CircularBlend >( start, end, bounds, waypoint, maxDeviation ),
      i_waypoint_( waypoint ), i_max_deviation_( maxDeviation )
{}

bool traxxs::path::CircularBlend::do_init()
{
  const Eigen::VectorXd startDirection = normalizedOrZero(i_waypoint_ - i_cond_start_.x);
  const Eigen::VectorXd endDirection = normalizedOrZero(i_cond_end_.x - i_waypoint_);
  
  if ( ( i_waypoint_ - i_cond_start_.x ).norm() < constants::kZero || ( i_cond_end_.x - i_waypoint_ ).norm() < constants::kZero 
        ||
        (startDirection - endDirection).norm() < constants::kZero // this is a i.Pi corner, no blend possible
  ) {
    length_ = 0.0;
    radius_ = 1.0;
    center_ = i_waypoint_;
    x_ = Eigen::VectorXd::Zero(center_.size());
    y_ = x_;
  } else {
    double distance = std::min((i_cond_start_.x - i_waypoint_).norm(), (i_cond_end_.x - i_waypoint_).norm());
    double cosangle = startDirection.dot(endDirection);
    cosangle = std::min( 1.0, std::max( -1.0, cosangle ) );
    const double angle = std::acos(cosangle);
    if( std::isnan( angle ) )
      throw std::runtime_error( "Path angle is invalid" );

    distance = std::min(distance, i_max_deviation_ * sin(0.5 * angle) / (1.0 - std::cos(0.5 * angle)));  // enforce max deviation

    radius_ = distance / tan(0.5 * angle);
    length_ = angle * radius_;

    center_ = i_waypoint_ + normalizedOrZero(endDirection - startDirection) * radius_ / std::cos(0.5 * angle);
    x_ = normalizedOrZero(i_waypoint_ - distance * startDirection - center_);
    y_ = startDirection;
  }
  
  return BlendSegment::do_init();
}


Eigen::VectorXd traxxs::path::CircularBlend::do_get_derivative( unsigned int order, double s ) const
{
  switch ( order ) {
    case 0: {
      return this->get_derivative_0( s );
      break;
    }
    case 1: {
      return this->get_derivative_1( s );
      break;
    }
    case 2: {
      return this->get_derivative_2( s );
      break;
    }
    case 3: {
      return this->get_derivative_3( s );
      break;
    }
    default: {
      throw std::runtime_error("Derivatives above 3 are not implemented for CircularBlend.");
      break;
    }
  }
}

Eigen::VectorXd traxxs::path::CircularBlend::do_get_derivative_cwise_abs_max( unsigned int order ) const
{
  Eigen::VectorXd tmp = Eigen::VectorXd::Zero(x_.size());
  switch ( order ) {
    case 1: {
      for ( unsigned int i=0; i<x_.size(); ++i )
        tmp[i] = sqrt( x_[i]*x_[i] + y_[i]*y_[i] );
      return tmp;
      break;
    }
    case 2: {
      for ( unsigned int i=0; i<x_.size(); ++i )
        tmp[i] = 1.0/radius_ * sqrt( x_[i]*x_[i] + y_[i]*y_[i] );
      return tmp;
      break;
    }
    case 3: {
      for ( unsigned int i=0; i<x_.size(); ++i )
        tmp[i] = 1.0/(radius_*radius_) * sqrt( x_[i]*x_[i] + y_[i]*y_[i] );
      return tmp;
      break;
    }
    default: {
      throw std::runtime_error("Derivatives above 3 are not implemented for CircularBlend.");
      break;
    }
  }
}

Eigen::VectorXd traxxs::path::CircularBlend::get_derivative_0(double s) const
{
  const double angle = s / radius_;
  return center_ + radius_ * (x_ * std::cos(angle) + y_ * std::sin(angle));
}

Eigen::VectorXd traxxs::path::CircularBlend::get_derivative_1(double s) const
{
  const double angle = s / radius_;
  return - x_ * std::sin(angle) + y_ * std::cos(angle);
}

Eigen::VectorXd traxxs::path::CircularBlend::get_derivative_2(double s) const
{
  const double angle = s / radius_;
  return - 1.0 / radius_ * (x_ * std::cos(angle) + y_ * std::sin(angle));
}

Eigen::VectorXd traxxs::path::CircularBlend::get_derivative_3(double s) const
{
  const double angle = s / radius_;
  return - 1.0 / (radius_*radius_) * (x_ * -std::sin(angle) + y_ * std::cos(angle));
}


traxxs::path::CartesianSegmentBase::CartesianSegmentBase( std::shared_ptr< PathSegment > segment_position, std::shared_ptr< PathSegment > segment_orientation, 
                        const Pose& start, const Pose& end ) 
{
  this->set( segment_position, segment_orientation, start, end );
}

bool traxxs::path::CartesianSegmentBase::set( std::shared_ptr< PathSegment > segment_position, std::shared_ptr< PathSegment > segment_orientation, 
                        const Pose& start, const Pose& end )
{
  cart_start_ = start, cart_end_ = end;  
  segment_pos_ = segment_position, segment_or_ = segment_orientation;
  return true;
}

bool traxxs::path::CartesianSegmentBase::do_init()
{
  StackedSegments::setSegments( std::vector< std::shared_ptr< PathSegment > >{ segment_pos_, segment_or_ } );
  StackedSegments::do_init();
  
  /** \fixme should not throw exception ( used in constructors ! ) */
  // check dimensions
  if ( segment_or_->getConfiguration(segment_or_->getLength()).size() != 1 )
    throw std::runtime_error( "Orientation segment segment_orientation should be of dimension 1 in CartesianSegmentBase." );
  
  // check that we have a match for position solely (orientation representation is different)
  if ( 
      ( segment_pos_->getConfiguration(0.0) - cart_start_.p ).norm() > constants::kZero 
      ||
      ( segment_pos_->getConfiguration(segment_pos_->getLength()) - cart_end_.p ).norm() > constants::kZero )
  {
    std::cout << segment_pos_->getConfiguration(0.0).transpose() << std::endl;
    std::cout << cart_start_.p.transpose() << std::endl;
    throw std::runtime_error( "Inconsistent position segment and start/end points in CartesianSegmentBase." );
  }
  
  or_trans_ = Eigen::AngleAxisd( cart_end_.q * cart_start_.q.inverse() );
  
  if (
      std::fabs( 0.0 - segment_or_->getConfiguration(0.0)[0] ) > constants::kZero // not starting at zero
      ||
      std::fabs( or_trans_.angle() - segment_or_->getConfiguration(segment_or_->getLength())[0] )  > constants::kZero ) // not ending at angle
    throw std::runtime_error( "Inconsistent orientation segment and start/end points in CartesianSegmentBase." );
  return true;
}


Eigen::VectorXd traxxs::path::CartesianSegmentBase::getPosition( const arc::ArcConditions& arc_conditions ) const {
  Eigen::VectorXd v4d = this->getConfiguration( arc_conditions.s );
  Pose p;
  p.p = v4d.segment( 0, 3 );
  /** \todo check that if v4d[3] == 0 => q = cart_start_.q AND v4d[3] = or_trans_.angle() => q = cart_end_.q */
  p.q = Eigen::AngleAxisd( v4d[3], or_trans_.axis() ) * cart_start_.q ; 
  return p.toVector();
}
Eigen::VectorXd traxxs::path::CartesianSegmentBase::getVelocity( const arc::ArcConditions& arc_conditions ) const {
  Eigen::VectorXd v4d = StackedSegments::getVelocity( arc_conditions );
  Eigen::VectorXd v( 6 );
  v.segment( 0, 3 ) = v4d.segment( 0, 3 );
  v.segment( 3, 3 ) = v4d[3] * or_trans_.axis();
  return v;
}
Eigen::VectorXd traxxs::path::CartesianSegmentBase::getAcceleration( const arc::ArcConditions& arc_conditions ) const {
  Eigen::VectorXd v4d = StackedSegments::getAcceleration( arc_conditions );
  Eigen::VectorXd a( 6 );
  a.segment( 0, 3 ) = v4d.segment( 0, 3 );
  a.segment( 3, 3 ) = v4d[3] * or_trans_.axis();
  return a;
}
Eigen::VectorXd traxxs::path::CartesianSegmentBase::getJerk( const arc::ArcConditions& arc_conditions ) const {
  Eigen::VectorXd v4d = StackedSegments::getJerk( arc_conditions );
  Eigen::VectorXd j( 6 );
  j.segment( 0, 3 ) = v4d.segment( 0, 3 );
  j.segment( 3, 3 ) = v4d[3] * or_trans_.axis();
  return j;
}


