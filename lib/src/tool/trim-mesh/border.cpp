/* This file is part of Dilay
 * Copyright © 2015-2017 Alexander Bau
 * Use and redistribute under the terms of the GNU General Public License
 */
#include "camera.hpp"
#include "intersection.hpp"
#include "maybe.hpp"
#include "primitive/plane.hpp"
#include "primitive/ray.hpp"
#include "tool/trim-mesh/border.hpp"
#include "util.hpp"

namespace
{
  typedef ToolTrimMeshBorderSegment::Polyline  Polyline;
  typedef ToolTrimMeshBorderSegment::Polylines Polylines;
}

struct ToolTrimMeshBorderSegment::Impl
{
  Polylines            polylines;
  const PrimPlane      plane;
  const Maybe<PrimRay> edge1;
  const Maybe<PrimRay> edge2;

  Impl (const PrimRay& e1, const PrimRay& e2)
    : plane (e1.origin (), glm::cross (e2.direction (), e1.direction ()))
    , edge1 (e1)
    , edge2 (e2)
  {
  }

  Impl (const PrimPlane& p, const PrimRay& e)
    : plane (p)
    , edge2 (e)
  {
  }

  Impl (const PrimRay& e, const PrimPlane& p)
    : plane (p)
    , edge1 (e)
  {
  }

  Impl (const PrimPlane& p)
    : plane (p)
  {
  }

  const PrimRay& edge () const
  {
    assert (this->edge2);
    return *this->edge2;
  }

  void addVertex (unsigned int index, const glm::vec3& p)
  {
    assert (this->onBorder (p));
    assert (this->polylines.empty () == false);

    this->polylines.back ().emplace_back (index);
  }

  void addPolyline () { this->polylines.emplace_back (); }

  void setNewIndices (const std::vector<unsigned int>& newIndices)
  {
    for (Polyline& p : this->polylines)
    {
      for (unsigned int& i : p)
      {
        assert (newIndices.at (i) != Util::invalidIndex ());

        i = newIndices.at (i);
      }
    }
  }

  bool isValidProjection (const glm::vec3& p) const
  {
    // assert (this->plane.onPlane (p)); // Occasionally fails due to rounding errors (?)

    const bool c1 =
      this->edge1
        ? 0.0f < glm::dot (this->plane.normal (),
                           glm::cross (p - this->edge1->origin (), this->edge1->direction ()))
        : true;

    const bool c2 =
      this->edge2
        ? 0.0f < glm::dot (this->plane.normal (),
                           glm::cross (this->edge2->direction (), p - this->edge2->origin ()))
        : true;
    return c1 && c2;
  }

  bool onBorder (const glm::vec3& p, bool* onEdge = nullptr) const
  {
    if (this->edge1 && this->edge1->onRay (p))
    {
      Util::setIfNotNull (onEdge, true);
      return true;
    }
    else if (this->edge2 && this->edge2->onRay (p))
    {
      Util::setIfNotNull (onEdge, true);
      return true;
    }
    else if (this->plane.onPlane (p))
    {
      Util::setIfNotNull (onEdge, false);
      return this->isValidProjection (p);
    }
    else
    {
      Util::setIfNotNull (onEdge, false);
      return false;
    }
  }

  bool intersects (const PrimRay& ray, float& t) const
  {
    if (IntersectionUtil::intersects (ray, this->plane, &t))
    {
      return this->isValidProjection (ray.pointAt (t));
    }
    else
    {
      return false;
    }
  }

  void deleteEmptyPolylines ()
  {
    this->polylines.erase (std::remove_if (this->polylines.begin (), this->polylines.end (),
                                           [](Polyline& p) { return p.empty (); }),
                           this->polylines.end ());
  }

  bool hasVertices () const
  {
    for (const Polyline& p : this->polylines)
    {
      if (p.empty () == false)
      {
        return true;
      }
    }
    return false;
  }
};

DELEGATE2_BIG3 (ToolTrimMeshBorderSegment, const PrimRay&, const PrimRay&)
DELEGATE2_CONSTRUCTOR (ToolTrimMeshBorderSegment, const PrimPlane&, const PrimRay&)
DELEGATE2_CONSTRUCTOR (ToolTrimMeshBorderSegment, const PrimRay&, const PrimPlane&)
DELEGATE1_CONSTRUCTOR (ToolTrimMeshBorderSegment, const PrimPlane&)
GETTER_CONST (const Polylines&, ToolTrimMeshBorderSegment, polylines)
GETTER_CONST (const PrimPlane&, ToolTrimMeshBorderSegment, plane)
DELEGATE_CONST (const PrimRay&, ToolTrimMeshBorderSegment, edge)
DELEGATE2 (void, ToolTrimMeshBorderSegment, addVertex, unsigned int, const glm::vec3&)
DELEGATE (void, ToolTrimMeshBorderSegment, addPolyline)
DELEGATE1 (void, ToolTrimMeshBorderSegment, setNewIndices, const std::vector<unsigned int>&)
DELEGATE2_CONST (bool, ToolTrimMeshBorderSegment, onBorder, const glm::vec3&, bool*)
DELEGATE2_CONST (bool, ToolTrimMeshBorderSegment, intersects, const PrimRay&, float&)
DELEGATE (void, ToolTrimMeshBorderSegment, deleteEmptyPolylines)
DELEGATE_CONST (bool, ToolTrimMeshBorderSegment, hasVertices)

struct ToolTrimMeshBorder::Impl
{
  DynamicMesh&                           mesh;
  std::vector<ToolTrimMeshBorderSegment> segments;

  Impl (DynamicMesh& m, const Camera& cam, const std::vector<glm::ivec2>& points, float offset,
        bool reverse)
    : mesh (m)
  {
    const unsigned int n = points.size ();
    assert (n >= 2);

    const PrimRay   rFirst = cam.ray (points[reverse ? (n - 1) : 0]);
    const PrimRay   rLast = cam.ray (points[reverse ? 0 : (n - 1)]);
    const glm::vec3 baseNormal =
      glm::normalize (glm::cross (rLast.direction (), rFirst.direction ()));

    const auto makePlane = [&cam, &points, offset, &baseNormal](unsigned int i1, unsigned int i2) {
      const PrimRay   r1 = cam.ray (points[i1]);
      const PrimRay   r2 = cam.ray (points[i2]);
      const glm::vec3 normal = glm::normalize (glm::cross (r2.direction (), r1.direction ()));
      const glm::vec3 point = cam.position () + (offset * baseNormal);
      return PrimPlane (point, normal);
    };

    const auto makeRay = [&cam, &points, offset, &baseNormal](unsigned int i) {
      const PrimRay r = cam.ray (points[i]);
      return PrimRay (r.origin () + (offset * baseNormal), r.direction ());
    };

    if (n == 2)
    {
      if (reverse == false)
      {
        this->segments.emplace_back (makePlane (0, 1));
      }
      else
      {
        this->segments.emplace_back (makePlane (1, 0));
      }
    }
    else
    {
      if (reverse == false)
      {
        this->segments.emplace_back (makePlane (0, 1), makeRay (1));
        for (unsigned int i = 1; i < n - 2; i++)
        {
          this->segments.emplace_back (makeRay (i), makeRay (i + 1));
        }
        this->segments.emplace_back (makeRay (n - 2), makePlane (n - 2, n - 1));
      }
      else
      {
        this->segments.emplace_back (makePlane (n - 1, n - 2), makeRay (n - 2));
        for (unsigned int i = n - 2; i > 1; i--)
        {
          this->segments.emplace_back (makeRay (i), makeRay (i - 1));
        }
        this->segments.emplace_back (makeRay (1), makePlane (1, 0));
      }
    }
  }

  const ToolTrimMeshBorderSegment& segment (unsigned int i) const
  {
    assert (i < this->numSegments ());
    return this->segments[i];
  }

  const ToolTrimMeshBorderSegment& getSegment (const glm::vec3& v1, const glm::vec3& v2) const
  {
    for (unsigned int i = 0; i < this->numSegments (); i++)
    {
      bool onEdge1 = false;
      bool onEdge2 = false;

      if (this->segments[i].onBorder (v1, &onEdge1) && this->segments[i].onBorder (v2, &onEdge2))
      {
        if (onEdge1 == false || onEdge2 == false)
        {
          return this->segments[i];
        }
      }
    }
    DILAY_IMPOSSIBLE
  }

  unsigned int numSegments () const { return this->segments.size (); }

  void addVertex (unsigned int index, const glm::vec3& p)
  {
#ifndef NDEBUG
    bool wasAdded = false;
#endif
    for (ToolTrimMeshBorderSegment& s : this->segments)
    {
      if (s.onBorder (p))
      {
        s.addVertex (index, p);
#ifndef NDEBUG
        wasAdded = true;
#endif
      }
    }
    assert (wasAdded);
  }

  void addPolyline ()
  {
    for (ToolTrimMeshBorderSegment& s : this->segments)
    {
      s.addPolyline ();
    }
  }

  void setNewIndices (const std::vector<unsigned int>& newIndices)
  {
    for (ToolTrimMeshBorderSegment& s : this->segments)
    {
      s.setNewIndices (newIndices);
    }
  }

  bool onBorder (const glm::vec3& p) const
  {
    for (const ToolTrimMeshBorderSegment& s : this->segments)
    {
      if (s.onBorder (p))
      {
        return true;
      }
    }
    return false;
  }

  bool trimVertex (const glm::vec3& p) const
  {
    if (this->onBorder (p))
    {
      return false;
    }
    else
    {
      glm::vec3 direction (0.0f);
      for (const ToolTrimMeshBorderSegment& s : this->segments)
      {
        direction -= s.plane ().normal ();
      }
      const PrimRay ray (p, direction);

      unsigned int n = 0;
      for (const ToolTrimMeshBorderSegment& s : this->segments)
      {
        float t;
        if (s.intersects (ray, t))
        {
          n++;
        }
      }
      return n % 2 == 1;
    }
  }

  bool trimFace (const glm::vec3& p1, const glm::vec3& p2, const glm::vec3& p3) const
  {
    if (this->trimVertex (p1) || this->trimVertex (p2) || this->trimVertex (p3))
    {
      return true;
    }
    else if (this->onBorder (p1) && this->onBorder (p2) && this->onBorder (p3))
    {
      const glm::vec3 p4 = Util::midpoint (p1, p2);
      const glm::vec3 p5 = Util::midpoint (p1, p3);
      const glm::vec3 p6 = Util::midpoint (p2, p3);

      return this->trimVertex (p4) || this->trimVertex (p5) || this->trimVertex (p6);
    }
    else
    {
      return false;
    }
  }

  void deleteEmptyPolylines ()
  {
    for (ToolTrimMeshBorderSegment& s : this->segments)
    {
      s.deleteEmptyPolylines ();
    }
  }

  bool hasVertices () const
  {
    for (const ToolTrimMeshBorderSegment& s : this->segments)
    {
      if (s.hasVertices ())
      {
        return true;
      }
    }
    return false;
  }

  bool onlyObtuseAngles () const
  {
    if (this->segments.size () > 2)
    {
      for (unsigned int i = 0; i < this->segments.size () - 1; i++)
      {
        const ToolTrimMeshBorderSegment& s = this->segments[i].plane ();
        const ToolTrimMeshBorderSegment& nextS = this->segments[i + 1].plane ();

        if (glm::dot (s.plane ().normal (), nextS.plane ().normal ()) < 0.0f)
        {
          return false;
        }
      }
    }
    return true;
  }
};

DELEGATE5_BIG2 (ToolTrimMeshBorder, DynamicMesh&, const Camera&, const std::vector<glm::ivec2>&,
                float, bool)
GETTER_CONST (DynamicMesh&, ToolTrimMeshBorder, mesh)
DELEGATE1_CONST (const ToolTrimMeshBorderSegment&, ToolTrimMeshBorder, segment, unsigned int)
DELEGATE2_CONST (const ToolTrimMeshBorderSegment&, ToolTrimMeshBorder, getSegment, const glm::vec3&,
                 const glm::vec3&)
DELEGATE_CONST (unsigned int, ToolTrimMeshBorder, numSegments)
DELEGATE2 (void, ToolTrimMeshBorder, addVertex, unsigned int, const glm::vec3&)
DELEGATE (void, ToolTrimMeshBorder, addPolyline)
DELEGATE1 (void, ToolTrimMeshBorder, setNewIndices, const std::vector<unsigned int>&)
DELEGATE1_CONST (bool, ToolTrimMeshBorder, onBorder, const glm::vec3&)
DELEGATE1_CONST (bool, ToolTrimMeshBorder, trimVertex, const glm::vec3&)
DELEGATE3_CONST (bool, ToolTrimMeshBorder, trimFace, const glm::vec3&, const glm::vec3&,
                 const glm::vec3&)
DELEGATE (void, ToolTrimMeshBorder, deleteEmptyPolylines)
DELEGATE_CONST (bool, ToolTrimMeshBorder, hasVertices)
DELEGATE_CONST (bool, ToolTrimMeshBorder, onlyObtuseAngles)
