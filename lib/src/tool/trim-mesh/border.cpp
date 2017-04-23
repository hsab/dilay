/* This file is part of Dilay
 * Copyright © 2015-2017 Alexander Bau
 * Use and redistribute under the terms of the GNU General Public License
 */
#include <algorithm>
#include <glm/gtx/norm.hpp>
#include "camera.hpp"
#include "intersection.hpp"
#include "maybe.hpp"
#include "primitive/aabox.hpp"
#include "primitive/plane.hpp"
#include "primitive/ray.hpp"
#include "tool/trim-mesh/border.hpp"
#include "util.hpp"

namespace {
  typedef ToolTrimMeshBorderSegment::Polyline  Polyline;
  typedef ToolTrimMeshBorderSegment::Polylines Polylines;
}

struct ToolTrimMeshBorderSegment::Impl {
        Polylines         polylines;
  const PrimPlane         plane;
  const Maybe <PrimRay>   edge1;
  const Maybe <PrimRay>   edge2;
  const Maybe <glm::vec3> u; 
  const Maybe <glm::vec3> v; 

  Impl (const PrimRay& e1, const PrimRay& e2)
    : plane  (e1.origin (), glm::cross (e2.direction (), e1.direction ()))
    , edge1  (e1)
    , edge2  (e2)
  {}

  Impl (const PrimPlane& p, const PrimRay& e)
    : plane (p)
    , edge2 (e)
  {}

  Impl (const PrimRay& e, const PrimPlane& p)
    : plane (p)
    , edge1 (e)
  {}

  Impl (const PrimPlane& p)
    : plane (p)
    , u     (Util::orthogonal (plane.normal ()))
    , v     (glm::cross (plane.normal (), *u))
  {}

  const PrimRay& edge () const {
    assert (this->edge2);
    return *this->edge2;
  }

  void addVertex (unsigned int index, const glm::vec3& p) {
    assert (this->onBorder (p));
    assert (this->polylines.empty () == false);

    this->polylines.back ().emplace_back (index);
  }

  void addPolyline () {
    this->polylines.emplace_back ();
  }

  void setNewIndices (const std::vector <unsigned int>& newIndices) {
    for (Polyline& p : this->polylines) {
      for (unsigned int& i : p) {
        assert (newIndices.at (i) != Util::invalidIndex ());

        i = newIndices.at (i);
      }
    }
  }

  bool isValidProjection (const glm::vec3& p) const {
    assert (this->plane.onPlane (p));

    const bool c1 = this->edge1 ? 0.0f < glm::dot ( this->plane.normal ()
                                                  , glm::cross ( p - this->edge1->origin ()
                                                               , this->edge1->direction () ) )
                                : true;

    const bool c2 = this->edge2 ? 0.0f < glm::dot ( this->plane.normal ()
                                                  , glm::cross ( this->edge2->direction ()
                                                               , p - this->edge2->origin () ) )
                                : true;
    return c1 && c2;
  }

  bool onBorder (const glm::vec3& p, bool* onEdge = nullptr) const {
    if (this->edge1 && this->edge1->onRay (p)) {
      Util::setIfNotNull (onEdge, true);
      return true;
    }
    else if (this->edge2 && this->edge2->onRay (p)) {
      Util::setIfNotNull (onEdge, true);
      return true;
    }
    else if (this->plane.onPlane (p)) {
      Util::setIfNotNull (onEdge, false);
      return this->isValidProjection (p);
    }
    else {
      Util::setIfNotNull (onEdge, false);
      return false;
    }
  }

  bool intersects (const PrimRay& ray, float& t) const {
    if (IntersectionUtil::intersects (ray, this->plane, &t)) {
      return this->isValidProjection (ray.pointAt (t));
    }
    else {
      return false;
    }
  }

  void deleteEmptyPolylines () {
    this->polylines.erase ( std::remove_if ( this->polylines.begin ()
                                           , this->polylines.end ()
                                           , [] (Polyline& p) { return p.empty (); } )
                          , this->polylines.end () );
  }
};

DELEGATE2_BIG3        (ToolTrimMeshBorderSegment, const PrimRay&, const PrimRay&)
DELEGATE2_CONSTRUCTOR (ToolTrimMeshBorderSegment, const PrimPlane&, const PrimRay&)
DELEGATE2_CONSTRUCTOR (ToolTrimMeshBorderSegment, const PrimRay&, const PrimPlane&)
DELEGATE1_CONSTRUCTOR (ToolTrimMeshBorderSegment, const PrimPlane&)
GETTER_CONST    (const Polylines&, ToolTrimMeshBorderSegment, polylines)
GETTER_CONST    (const PrimPlane&, ToolTrimMeshBorderSegment, plane)
DELEGATE_CONST  (const PrimRay&,   ToolTrimMeshBorderSegment, edge)
DELEGATE2       (void,             ToolTrimMeshBorderSegment, addVertex, unsigned int, const glm::vec3&)
DELEGATE        (void,             ToolTrimMeshBorderSegment, addPolyline)
DELEGATE1       (void,             ToolTrimMeshBorderSegment, setNewIndices, const std::vector <unsigned int>&)
DELEGATE2_CONST (bool,             ToolTrimMeshBorderSegment, onBorder, const glm::vec3&, bool*)
DELEGATE2_CONST (bool,             ToolTrimMeshBorderSegment, intersects, const PrimRay&, float&)
DELEGATE        (void,             ToolTrimMeshBorderSegment, deleteEmptyPolylines)

struct ToolTrimMeshBorder::Impl {
  const glm::vec3                         cameraPosition;
  const PrimPlane                         borderPlane;
  std::vector <ToolTrimMeshBorderSegment> segments;

  Impl (const Camera& cam, const std::vector <glm::ivec2>& points)
    : cameraPosition (cam.position ())
    , borderPlane    (cam.position (), glm::cross ( cam.ray (points[1]).direction ()
                                                  , cam.ray (points[0]).direction () ))
  {
    assert (points.size () >= 2);

    if (points.size () == 2) {
      segments.emplace_back (this->borderPlane);
    }
    else {
      auto addFirstSegment = [this, &cam, &points] () {
        const PrimRay   r1 = cam.ray (points[0]);
        const PrimRay   r2 = cam.ray (points[1]);
        const PrimPlane p  = PrimPlane (cam.position (), glm::cross ( r2.direction ()
                                                                    , r1.direction () ));
        this->segments.emplace_back (p, r2);
      };

      auto addMiddleSegment = [this, &cam, &points] (auto it) {
        const PrimRay r1 = cam.ray (*it);
        const PrimRay r2 = cam.ray (*std::next (it));

        this->segments.emplace_back (r1, r2);
      };

      auto addLastSegment = [this, &cam, &points] () {
        const PrimRay   r1 = cam.ray (*std::prev (std::prev (points.end ())));
        const PrimRay   r2 = cam.ray (*std::prev (points.end ()));
        const PrimPlane p  = PrimPlane (cam.position (), glm::cross ( r2.direction ()
                                                                    , r1.direction () ));
        this->segments.emplace_back (r1, p);
      };

      addFirstSegment ();
      for (auto it = std::next (points.begin ()); std::next (it) != std::prev (points.end ()); ++it) {
        addMiddleSegment (it);
      }
      addLastSegment ();
    }
  }

  const ToolTrimMeshBorderSegment& segment (unsigned int i) const {
    assert (i < this->numSegments ());
    return this->segments[i];
  }

  const ToolTrimMeshBorderSegment& getSegment (const glm::vec3& v1, const glm::vec3& v2) const {
    for (unsigned int i = 0; i < this->numSegments (); i++) {
      bool onEdge1 = false;
      bool onEdge2 = false;

      if (this->segments[i].onBorder (v1, &onEdge1) && this->segments[i].onBorder (v2, &onEdge2)) {
        if (onEdge1 == false || onEdge2 == false) {
          return this->segments[i];
        }
      }
    }
    DILAY_IMPOSSIBLE
  }

  unsigned int numSegments () const {
    return this->segments.size ();
  }

  void addVertex (unsigned int index, const glm::vec3& p) {
#ifndef NDEBUG
    bool wasAdded = false;
#endif
    for (ToolTrimMeshBorderSegment& s : this->segments) {
      if (s.onBorder (p)) {
        s.addVertex (index, p);
#ifndef NDEBUG
        wasAdded = true;
#endif
      }
    }
    assert (wasAdded);
  }

  void addPolyline () {
    for (ToolTrimMeshBorderSegment& s : this->segments) {
      s.addPolyline ();
    }
  }

  void setNewIndices (const std::vector <unsigned int>& newIndices) {
    for (ToolTrimMeshBorderSegment& s : this->segments) {
      s.setNewIndices (newIndices);
    }
  }

  bool onBorder (const glm::vec3& p) const {
    for (const ToolTrimMeshBorderSegment& s : this->segments) {
      if (s.onBorder (p)) {
        return true;
      }
    }
    return false;
  }

  bool trimVertex (const glm::vec3& p) const {
    if (this->onBorder (p)) {
      return false;
    }
    else {
      const PrimRay ray (p, -this->borderPlane.normal ());
      unsigned int n = 0;

      for (const ToolTrimMeshBorderSegment& s : this->segments) {
        float t;
        if (s.intersects (ray, t)) {
          n++;
        }
      }
      return n % 2 == 1;
    }
  }

  bool trimFace (const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3) const {
    if (this->trimVertex (p1) || this->trimVertex (p2) || this->trimVertex (p3)) {
      return true;
    }
    else if (this->onBorder (p1) && this->onBorder (p2) && this->onBorder (p3)) {
      const glm::vec3 p4 = Util::between (p1, p2);
      const glm::vec3 p5 = Util::between (p1, p3);
      const glm::vec3 p6 = Util::between (p2, p3);

      return this->trimVertex (p4) || this->trimVertex (p5) || this->trimVertex (p6);
    }
    else {
      return false;
    }
  }

  void deleteEmptyPolylines () {
    for (ToolTrimMeshBorderSegment& s : this->segments) {
      s.deleteEmptyPolylines ();
    }
  }
};

DELEGATE2_BIG2 (ToolTrimMeshBorder, const Camera&, const std::vector <glm::ivec2>&)
DELEGATE1_CONST  (const ToolTrimMeshBorderSegment&, ToolTrimMeshBorder, segment, unsigned int)
DELEGATE2_CONST  (const ToolTrimMeshBorderSegment&, ToolTrimMeshBorder, getSegment, const glm::vec3&, const glm::vec3&)
GETTER_CONST     (const glm::vec3&, ToolTrimMeshBorder, cameraPosition)
DELEGATE_CONST   (unsigned int,     ToolTrimMeshBorder, numSegments)
DELEGATE2        (void,             ToolTrimMeshBorder, addVertex, unsigned int, const glm::vec3&)
DELEGATE         (void,             ToolTrimMeshBorder, addPolyline)
DELEGATE1        (void,             ToolTrimMeshBorder, setNewIndices, const std::vector <unsigned int>&)
DELEGATE1_CONST  (bool,             ToolTrimMeshBorder, onBorder, const glm::vec3&)
DELEGATE1_CONST  (bool,             ToolTrimMeshBorder, trimVertex, const glm::vec3&)
DELEGATE3_CONST  (bool,             ToolTrimMeshBorder, trimFace, const glm::vec3&, const glm::vec3&, const glm::vec3&)
DELEGATE         (void,             ToolTrimMeshBorder, deleteEmptyPolylines)
