/* -*-c++-*- OpenSceneGraph - Copyright (C) 1998-2003 Robert Osfield 
 *
 * This library is open source and may be redistributed and/or modified under  
 * the terms of the OpenSceneGraph Public License (OSGPL) version 0.0 or 
 * (at your option) any later version.  The full license is in LICENSE file
 * included with this distribution, and on the openscenegraph.org website.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
 * OpenSceneGraph Public License for more details.
*/

#include <osg/TriangleIndexFunctor>

#include <osgUtil/Simplifier>

#include <osgUtil/SmoothingVisitor>
#include <osgUtil/TriStripVisitor>

#include <set>
#include <list>

using namespace osgUtil;


struct dereference_less
{
    template<class T>
    inline bool operator() (const T& lhs,const T& rhs) const
    {
        return *lhs < *rhs;
    }
};

struct dereference_clear
{
    template<class T>
    inline void operator() (const T& t)
    {
        T& non_const_t = const_cast<T&>(t);
        return non_const_t->clear();
    }
};

class EdgeCollapse
{
public:

    struct Triangle;
    struct Edge;
    struct Point;


    EdgeCollapse() {}
        
    ~EdgeCollapse();

    void setGeometry(osg::Geometry* geometry, const Simplifier::IndexList& protectedPoints);
    osg::Geometry* getGeometry() { return _geometry; }

    unsigned int getNumOfTriangles() { return _triangleSet.size(); }

    Point* computeInterpolatedPoint(Edge* edge,float r) const
    {
        Point* point = new Point;
        float r1 = 1.0f-r;
        float r2 = r;
        Point* p1 = edge->_p1.get();
        Point* p2 = edge->_p2.get();
        
        if (p1==0 || p2==0)
        {
            osg::notify(osg::NOTICE)<<"Error computeInterpolatedPoint("<<edge<<",r) p1 and/or p2==0"<<std::endl;
            return 0;
        } 
        
        point->_vertex = p1->_vertex * r1 + p2->_vertex * r2;
        unsigned int s = osg::minimum(p1->_attributes.size(),p2->_attributes.size());
        for(unsigned int i=0;i<s;++i)
        {
            point->_attributes.push_back(p1->_attributes[i]*r1 + p2->_attributes[i]*r2); 
        }
        return point;
    }

    Point* computeOptimalPoint(Edge* edge) const
    {
        return computeInterpolatedPoint(edge,0.5f);
    }
    
    float computeErrorMetric(Edge* edge,Point* point) const
    {
        typedef std::set< osg::ref_ptr<Triangle> > LocalTriangleSet ;
        LocalTriangleSet triangles;
        std::copy( edge->_p1->_triangles.begin(), edge->_p1->_triangles.end(), std::inserter(triangles,triangles.begin()));
        std::copy( edge->_p2->_triangles.begin(), edge->_p2->_triangles.end(), std::inserter(triangles,triangles.begin()));
    
        const osg::Vec3& vertex = point->_vertex;
        float error = 0.0f;
        
        if (triangles.empty()) return 0.0f;
        
        for(LocalTriangleSet::iterator itr=triangles.begin();
            itr!=triangles.end();
            ++itr)
        {
            error += fabs( (*itr)->distance(vertex) );
        }
        
        // use average of error
        error /= (float)triangles.size();
        
        return error;
    }
    
    void updateErrorMetricForEdge(Edge* edge)
    {
        if (!edge->_p1 || !edge->_p2)
        {
            osg::notify(osg::NOTICE)<<"Error updateErrorMetricForEdge("<<edge<<") p1 and/or p2==0"<<std::endl;
            return;
        }


        osg::ref_ptr<Edge> keep_local_reference_to_edge(edge);
    
        if (_edgeSet.count(keep_local_reference_to_edge)!=0)
        {
            _edgeSet.erase(keep_local_reference_to_edge);
        }
        
        
        edge->_proposedPoint = computeOptimalPoint(edge);
        edge->updateMaxNormalDeviationOnEdgeCollapse();
        
        if (edge->getMaxNormalDeviationOnEdgeCollapse()<=1.0f && !edge->isAdjacentToBoundary())
            edge->setErrorMetric( computeErrorMetric( edge, edge->_proposedPoint.get()));
        else
            edge->setErrorMetric( FLT_MAX );
        
        _edgeSet.insert(keep_local_reference_to_edge);
    }
    
    void updateErrorMetricForAllEdges()
    {
        typedef std::vector< osg::ref_ptr<Edge> > LocalEdgeList ;
        LocalEdgeList edges;
        std::copy( _edgeSet.begin(), _edgeSet.end(), std::back_inserter(edges));
        
        _edgeSet.clear();
        
        for(LocalEdgeList::iterator itr=edges.begin();
            itr!=edges.end();
            ++itr)
        {
            Edge* edge = itr->get();

        edge->_proposedPoint = computeOptimalPoint(edge);
        edge->updateMaxNormalDeviationOnEdgeCollapse();
        
        if (edge->getMaxNormalDeviationOnEdgeCollapse()<=1.0f && !edge->isAdjacentToBoundary())
            edge->setErrorMetric( computeErrorMetric( edge, edge->_proposedPoint.get()));
        else
            edge->setErrorMetric( FLT_MAX );
            
            _edgeSet.insert(edge);
        }
    }

    bool collapseMinimumErrorEdge()
    {
        if (!_edgeSet.empty())
        {
            Edge* edge = const_cast<Edge*>(_edgeSet.begin()->get());

            if (edge->getErrorMetric()==FLT_MAX)
            {
                osg::notify(osg::WARN)<<"collapseMinimumErrorEdge() return false due to edge->getErrorMetric()==FLT_MAX"<<std::endl;
                return false;
            }

            osg::ref_ptr<Point> pNew = edge->_proposedPoint.valid()? edge->_proposedPoint : computeInterpolatedPoint(edge,0.5f);
            return (collapseEdge(edge,pNew.get()));
        }
        osg::notify(osg::WARN)<<"collapseMinimumErrorEdge() return false due to _edgeSet.empty()"<<std::endl;
        return false;
    }

    void copyBackToGeometry();

    typedef std::vector<float>                              FloatList;
    typedef std::set<osg::ref_ptr<Edge>,dereference_less>   EdgeSet;
    typedef std::set< osg::ref_ptr<Point>,dereference_less> PointSet;
    typedef std::vector< osg::ref_ptr<Point> >              PointList;
    typedef std::list< osg::ref_ptr<Triangle> >             TriangleList;
    typedef std::set< osg::ref_ptr<Triangle> >              TriangleSet;
    typedef std::map< osg::ref_ptr<Triangle>, unsigned int, dereference_less > TriangleMap;

    struct Point : public osg::Referenced
    {
        Point(): _protected(false), _index(0) {}
        
        bool _protected;

        unsigned int _index;

        osg::Vec3           _vertex;
        FloatList           _attributes;
        TriangleSet         _triangles;

        void clear()
        {
            _attributes.clear();
            _triangles.clear();
        }

        bool operator < ( const Point& rhs) const
        {
            if (_vertex < rhs._vertex) return true;
            if (rhs._vertex < _vertex) return false;
            
            return _attributes < rhs._attributes;
        }
        
        bool isBoundaryPoint() const
        {
            if (_protected) return true;
        
            for(TriangleSet::const_iterator itr=_triangles.begin();
                itr!=_triangles.end();
                ++itr)
            {
                const Triangle* triangle = itr->get();
                if ((triangle->_e1->_p1==this || triangle->_e1->_p2==this) && triangle->_e1->isBoundaryEdge()) return true;
                if ((triangle->_e2->_p1==this || triangle->_e2->_p2==this) && triangle->_e2->isBoundaryEdge()) return true;
                if ((triangle->_e3->_p1==this || triangle->_e3->_p2==this) && triangle->_e3->isBoundaryEdge()) return true;
            
                //if ((*itr)->isBoundaryTriangle()) return true;
            }
            return false;
        }

    };

    struct Edge : public osg::Referenced
    {
        Edge(): _errorMetric(0.0f), _maximumDeviation(1.0f) {}
        
        void clear()
        {
            _p1 = 0;
            _p2 = 0;
            _triangles.clear();
        }

        osg::ref_ptr<Point> _p1;
        osg::ref_ptr<Point> _p2;
        
        TriangleSet _triangles;

        float _errorMetric;
        float _maximumDeviation;

        osg::ref_ptr<Point> _proposedPoint;

        void setErrorMetric(float errorMetric) { _errorMetric = errorMetric; }
        float getErrorMetric() const { return _errorMetric; }
        
        bool operator < ( const Edge& rhs) const
        {
            // both error metrics are computed
            if (getErrorMetric()<rhs.getErrorMetric()) return true;
            else if (rhs.getErrorMetric()<getErrorMetric()) return false;
            
            if (_p1 < rhs._p1) return true;
            else if (rhs._p1 < _p1) return false;

            return (_p2 < rhs._p2);
        }
        
        bool operator == ( const Edge& rhs) const
        {
            if (&rhs==this) return true;
            if (*this<rhs) return false;
            if (rhs<*this) return false;
            return true;
        }

        bool operator != ( const Edge& rhs) const
        {
            if (&rhs==this) return false;
            if (*this<rhs) return true;
            if (rhs<*this) return true;
            return false;
        }
        
        void addTriangle(Triangle* triangle)
        {
            _triangles.insert(triangle);
            // if (_triangles.size()>2) osg::notify(osg::NOTICE)<<"Warning too many traingles ("<<_triangles.size()<<") sharing edge "<<std::endl;
        }
        
        bool isBoundaryEdge() const
        {
            return _triangles.size()<=1;
        }
        
        bool isAdjacentToBoundary() const
        {
            return isBoundaryEdge() || _p1->isBoundaryPoint() || _p2->isBoundaryPoint(); 
        }
        

        void updateMaxNormalDeviationOnEdgeCollapse()
        {
            //osg::notify(osg::NOTICE)<<"updateMaxNormalDeviationOnEdgeCollapse()"<<std::endl;
            _maximumDeviation = 0.0f;
            for(TriangleSet::iterator itr1=_p1->_triangles.begin();
                itr1!=_p1->_triangles.end();
                ++itr1)
            {
                if (_triangles.count(*itr1)==0)
                {
                    _maximumDeviation = osg::maximum(_maximumDeviation, (*itr1)->computeNormalDeviationOnEdgeCollapse(this,_proposedPoint.get()));
                }
            }
            for(TriangleSet::iterator itr2=_p2->_triangles.begin();
                itr2!=_p2->_triangles.end();
                ++itr2)
            {
                if (_triangles.count(*itr2)==0)
                {
                    _maximumDeviation = osg::maximum(_maximumDeviation, (*itr2)->computeNormalDeviationOnEdgeCollapse(this,_proposedPoint.get()));
                }
            }
        }
        
        float getMaxNormalDeviationOnEdgeCollapse() const { return _maximumDeviation; } 

    };

    struct Triangle : public osg::Referenced
    {
        Triangle() {}
        
        void clear()
        {
            _p1 = 0;
            _p2 = 0;
            _p3 = 0;
        
            _e1 = 0;
            _e2 = 0;
            _e3 = 0;
        }

        inline bool operator < (const Triangle& rhs) const
        {
            if (_p1 < rhs._p1) return true;
            if (rhs._p1 < _p1) return false;


            const Point* lhs_lower = _p2<_p3 ? _p2.get() : _p3.get(); 
            const Point* rhs_lower = rhs._p2<rhs._p3 ? rhs._p2.get() : rhs._p3.get(); 

            if (lhs_lower < rhs_lower) return true;
            if (rhs_lower < lhs_lower) return false;

            const Point* lhs_upper = _p2<_p3 ? _p3.get() : _p2.get(); 
            const Point* rhs_upper = rhs._p2<rhs._p3 ? rhs._p3.get() : rhs._p2.get(); 

            return (lhs_upper < rhs_upper);
        }


        void setOrderedPoints(Point* p1, Point* p2, Point* p3)
        {
            Point* points[3];
            points[0] = p1;
            points[1] = p2;
            points[2] = p3;

            // find the lowest value point in the list.
            unsigned int lowest = 0;        
            if (points[1]<points[lowest]) lowest = 1;
            if (points[2]<points[lowest]) lowest = 2;


            _p1 = points[lowest];
            _p2 = points[(lowest+1)%3];
            _p3 = points[(lowest+2)%3];
        }
        
        void update()
        {
            _plane.set(_p1->_vertex,_p2->_vertex,_p3->_vertex);
            
        }
        
        osg::Plane computeNewPlaneOnEdgeCollapse(Edge* edge,Point* pNew) const
        {
            const Point* p1 = (_p1==edge->_p1 || _p1==edge->_p2) ? pNew : _p1.get();  
            const Point* p2 = (_p2==edge->_p1 || _p2==edge->_p2) ? pNew : _p2.get();  
            const Point* p3 = (_p3==edge->_p1 || _p3==edge->_p2) ? pNew : _p3.get();
            
            return osg::Plane(p1->_vertex,p2->_vertex,p3->_vertex);
        }
        
        // note return 1 - dotproduct, so that deviation is in the range of 0.0 to 2.0, where 0 is coincendent, 1.0 is 90 degrees, and 2.0 is 180 degrees.
        float computeNormalDeviationOnEdgeCollapse(Edge* edge,Point* pNew) const
        {
            const Point* p1 = (_p1==edge->_p1 || _p1==edge->_p2) ? pNew : _p1.get();  
            const Point* p2 = (_p2==edge->_p1 || _p2==edge->_p2) ? pNew : _p2.get();  
            const Point* p3 = (_p3==edge->_p1 || _p3==edge->_p2) ? pNew : _p3.get();
            
            osg::Vec3 new_normal = (p2->_vertex - p1->_vertex) ^ (p3->_vertex - p2->_vertex);
            new_normal.normalize();
            
            float result = 1.0f - (new_normal.x() * _plane[0] + new_normal.y() * _plane[1] + new_normal.z() * _plane[2]);
            return result;
        }

        float distance(const osg::Vec3& vertex) const
        {
            return _plane.distance(vertex);
        }
        
        bool isBoundaryTriangle() const
        {
            return (_e1->isBoundaryEdge() || _e2->isBoundaryEdge() ||  _e3->isBoundaryEdge());
        }

       
        osg::ref_ptr<Point> _p1;
        osg::ref_ptr<Point> _p2;
        osg::ref_ptr<Point> _p3;
        
        osg::ref_ptr<Edge> _e1;
        osg::ref_ptr<Edge> _e2;
        osg::ref_ptr<Edge> _e3;
        
        osg::Plane _plane;

    };


    Triangle* addTriangle(unsigned int p1, unsigned int p2, unsigned int p3)
    {
        //osg::notify(osg::NOTICE)<<"addTriangle("<<p1<<","<<p2<<","<<p3<<")"<<std::endl;

        // detect if triangle is degenerate.
        if (p1==p2 || p2==p3 || p1==p3) return 0;
        
        Triangle* triangle = new Triangle;

        Point* points[3];
        points[0] = addPoint(triangle, p1);
        points[1] = addPoint(triangle, p2);
        points[2] = addPoint(triangle, p3);
        
        // find the lowest value point in the list.
        unsigned int lowest = 0;        
        if (points[1]<points[lowest]) lowest = 1;
        if (points[2]<points[lowest]) lowest = 2;
        

        triangle->_p1 = points[lowest];
        triangle->_p2 = points[(lowest+1)%3];
        triangle->_p3 = points[(lowest+2)%3];

        triangle->_e1 = addEdge(triangle, triangle->_p1.get(), triangle->_p2.get());
        triangle->_e2 = addEdge(triangle, triangle->_p2.get(), triangle->_p3.get());
        triangle->_e3 = addEdge(triangle, triangle->_p3.get(), triangle->_p1.get());
        
        triangle->update();

        _triangleSet.insert(triangle);
        
        return triangle;
    }
    
    Triangle* addTriangle(Point* p1, Point* p2, Point* p3)
    {
        // osg::notify(osg::NOTICE)<<"addTriangle("<<p1<<","<<p2<<","<<p3<<")"<<std::endl;

        // detect if triangle is degenerate.
        if (p1==p2 || p2==p3 || p1==p3) 
        {
            osg::notify(osg::NOTICE)<<"    **** addTriangle failed - p1==p2 || p2==p3 || p1==p3"<<std::endl;
            return 0;
        }
        
        Triangle* triangle = new Triangle;

        Point* points[3];
        points[0] = addPoint(triangle, p1);
        points[1] = addPoint(triangle, p2);
        points[2] = addPoint(triangle, p3);
        
        // find the lowest value point in the list.
        unsigned int lowest = 0;        
        if (points[1]<points[lowest]) lowest = 1;
        if (points[2]<points[lowest]) lowest = 2;
        

        triangle->_p1 = points[lowest];
        triangle->_p2 = points[(lowest+1)%3];
        triangle->_p3 = points[(lowest+2)%3];

        triangle->_e1 = addEdge(triangle, triangle->_p1.get(), triangle->_p2.get());
        triangle->_e2 = addEdge(triangle, triangle->_p2.get(), triangle->_p3.get());
        triangle->_e3 = addEdge(triangle, triangle->_p3.get(), triangle->_p1.get());
        
        triangle->update();

        _triangleSet.insert(triangle);

        return triangle;
    }

    void removeTriangle(Triangle* triangle)
    {
        if (triangle->_p1.valid()) removePoint(triangle,triangle->_p1.get());
        if (triangle->_p2.valid()) removePoint(triangle,triangle->_p2.get());
        if (triangle->_p3.valid()) removePoint(triangle,triangle->_p3.get());
        
        if (triangle->_e1.valid()) removeEdge(triangle,triangle->_e1.get());
        if (triangle->_e2.valid()) removeEdge(triangle,triangle->_e2.get());
        if (triangle->_e3.valid()) removeEdge(triangle,triangle->_e3.get());

        _triangleSet.erase(triangle);
    }

    void replaceTrianglePoint(Triangle* triangle, Point* pOriginal, Point* pNew)
    {
        if (triangle->_p1==pOriginal || triangle->_p2==pOriginal || triangle->_p3==pOriginal)
        {
            // fix the corner points to use the new point
            if (triangle->_p1==pOriginal) triangle->_p1=pNew;
            if (triangle->_p2==pOriginal) triangle->_p2=pNew;
            if (triangle->_p3==pOriginal) triangle->_p3=pNew;
            
            // fixes the edges so they point to use the new point
            triangle->_e1 = replaceEdgePoint(triangle->_e1.get(),pOriginal,pNew);
            triangle->_e2 = replaceEdgePoint(triangle->_e2.get(),pOriginal,pNew);
            triangle->_e3 = replaceEdgePoint(triangle->_e3.get(),pOriginal,pNew);
            
            // remove the triangle form the orignal point, and possibly the point if its the last triangle to use it
            removePoint(triangle, pOriginal);
            
            // add the triangle to that point
            addPoint(triangle,pNew);
        }
        
    }
    
    unsigned int testTriangle(Triangle* triangle)
    {
        unsigned int result = 0;
        if (!(triangle->_p1))
        {
            osg::notify(osg::NOTICE)<<"testTriangle("<<triangle<<") _p1==NULL"<<std::endl;
            ++result;
        }
        else if (triangle->_p1->_triangles.count(triangle)==0) 
        {
            osg::notify(osg::NOTICE)<<"testTriangle("<<triangle<<") _p1->_triangles does not contain triangle"<<std::endl;
            ++result;
        }

        if (!(triangle->_p2))
        {
            osg::notify(osg::NOTICE)<<"testTriangle("<<triangle<<") _p2==NULL"<<std::endl;
            ++result;
        }
        else if (triangle->_p2->_triangles.count(triangle)==0) 
        {
            osg::notify(osg::NOTICE)<<"testTriangle("<<triangle<<") _p2->_triangles does not contain triangle"<<std::endl;
            ++result;
        }

        if (!(triangle->_p3))
        {
            osg::notify(osg::NOTICE)<<"testTriangle("<<triangle<<") _p3==NULL"<<std::endl;
            ++result;
        }
        else if (triangle->_p3->_triangles.count(triangle)==0) 
        {
            osg::notify(osg::NOTICE)<<"testTriangle("<<triangle<<") _p3->_triangles does not contain triangle"<<std::endl;
            ++result;
        }
        
        if (testEdge(triangle->_e1.get()))
        {
            ++result;
            osg::notify(osg::NOTICE)<<"testTriangle("<<triangle<<") _e1 test failed"<<std::endl;
        }
        
        if (testEdge(triangle->_e2.get()))
        {
            ++result;
            osg::notify(osg::NOTICE)<<"testTriangle("<<triangle<<") _e2 test failed"<<std::endl;
        }

        if (testEdge(triangle->_e3.get()))
        {
            osg::notify(osg::NOTICE)<<"testTriangle("<<triangle<<") _e3 test failed"<<std::endl;
            ++result;
        }

        return result;
    }

    unsigned int testAllTriangles()
    {
        unsigned int numErrors = 0;
        for(TriangleSet::iterator itr=_triangleSet.begin();
            itr!=_triangleSet.end();
            ++itr)
        {
            numErrors += testTriangle(const_cast<Triangle*>(itr->get()));
        }
        return numErrors;
    }

    Edge* addEdge(Triangle* triangle, Point* p1, Point* p2)
    {
        //osg::notify(osg::NOTICE)<<"addEdge("<<p1<<","<<p2<<")"<<std::endl;
        osg::ref_ptr<Edge> edge = new Edge;
        if (p1<p2)
        {
            edge->_p1 = p1;
            edge->_p2 = p2;
        }
        else
        {
            edge->_p1 = p2;
            edge->_p2 = p1;
        }
        
        EdgeSet::iterator itr = _edgeSet.find(edge);
        if (itr==_edgeSet.end())
        {
            // osg::notify(osg::NOTICE)<<"  addEdge("<<edge.get()<<") edge->_p1="<<edge->_p1.get()<<" _p2="<<edge->_p2.get()<<std::endl;
            _edgeSet.insert(edge);
        }
        else
        {
            // osg::notify(osg::NOTICE)<<"  reuseEdge("<<edge.get()<<") edge->_p1="<<edge->_p1.get()<<" _p2="<<edge->_p2.get()<<std::endl;
            edge = *itr;
        }
        
        edge->addTriangle(triangle);
        
        return edge.get();
    }

    void removeEdge(Triangle* triangle, Edge* edge)
    {
        EdgeSet::iterator itr = _edgeSet.find(edge);
        if (itr!=_edgeSet.end())
        {
            edge->_triangles.erase(triangle);
            if (edge->_triangles.empty())
            {
                // edge no longer in use, so need to delete.
                _edgeSet.erase(itr);

                edge->_p1 = 0;
                edge->_p2 = 0;
            }
        }
    }

    Edge* replaceEdgePoint(Edge* edge, Point* pOriginal, Point* pNew)
    {
        if (edge->_p1==pOriginal || edge->_p2==pOriginal)
        {
            EdgeSet::iterator itr = _edgeSet.find(edge);
            if (itr!=_edgeSet.end())
            {
                // remove the edge from the list, as its positoin in the list
                // may need to change once its values have been ammended 
                _edgeSet.erase(itr);
            }
            
            // modify its values
            if (edge->_p1==pOriginal) edge->_p1=pNew;
            if (edge->_p2==pOriginal) edge->_p2=pNew;

            if (edge->_p2 > edge->_p1)
            {
                // swap to ensure that lowest ptr value of p1, p2 pair is first.
                osg::ref_ptr<Point> tmp = edge->_p1;
                edge->_p1 = edge->_p2;
                edge->_p2 = tmp;
            }

            itr = _edgeSet.find(edge);
            if (itr!=_edgeSet.end())
            {
                // reuse existing edge.
                edge = const_cast<Edge*>(itr->get());
            }
            else
            {
                // put it back in.
                _edgeSet.insert(edge);
            }
            return edge;
        }
        else
        {
            return edge;
        }
        
    }


    bool collapseEdge(Edge* edge, Point* pNew)
    {
        //if (edge->_triangles.size()<2) return false;
        //if (edge->_triangles.size()>2) return false;

        if (edge->getMaxNormalDeviationOnEdgeCollapse()>1.0)
        {
//            osg::notify(osg::NOTICE)<<"collapseEdge("<<edge<<") refused due to edge->getMaxNormalDeviationOnEdgeCollapse() = "<<edge->getMaxNormalDeviationOnEdgeCollapse()<<std::endl;
            return false;
        }
        else
        {
            //osg::notify(osg::NOTICE)<<"collapseEdge("<<edge<<") edge->getMaxNormalDeviationOnEdgeCollapse() = "<<edge->getMaxNormalDeviationOnEdgeCollapse()<<std::endl;
        }


        typedef std::set< osg::ref_ptr<Edge> > LocalEdgeList;

        osg::ref_ptr<Edge> keep_edge_locally_referenced_to_prevent_premature_deletion = edge;
        osg::ref_ptr<Point> keep_point_locally_referenced_to_prevent_premature_deletion = pNew;
        osg::ref_ptr<Point> edge_p1 = edge->_p1;
        osg::ref_ptr<Point> edge_p2 = edge->_p2;
        
        TriangleMap  triangleMap;
        TriangleList triangles_p1;
        TriangleList triangles_p2;
        LocalEdgeList oldEdges;
        
        
        if (edge_p1 != pNew)
        {
            for(TriangleSet::iterator itr=edge_p1->_triangles.begin();
                itr!=edge_p1->_triangles.end();
                ++itr)
            {
                if (edge->_triangles.count(*itr)==0)
                {
                    Triangle* triangle = const_cast<Triangle*>(itr->get());
                    triangles_p1.push_back(triangle);
                    oldEdges.insert(triangle->_e1);
                    oldEdges.insert(triangle->_e2);
                    oldEdges.insert(triangle->_e3);
                }
            }
            
            //triangles_p1 = edge_p1->_triangles;
        }
                
        if (edge_p2 != pNew)
        {
            for(TriangleSet::iterator itr=edge_p2->_triangles.begin();
                itr!=edge_p2->_triangles.end();
                ++itr)
            {
                if (edge->_triangles.count(*itr)==0)
                {
                    Triangle* triangle = const_cast<Triangle*>(itr->get());
                    triangles_p2.push_back(triangle);
                    oldEdges.insert(triangle->_e1);
                    oldEdges.insert(triangle->_e2);
                    oldEdges.insert(triangle->_e3);
                }
            }
            //triangles_p2 = edge_p2->_triangles;
        }

        for(LocalEdgeList::iterator oeitr=oldEdges.begin();
            oeitr!=oldEdges.end();
            ++oeitr)
        {
            _edgeSet.erase(*oeitr);
            
            const_cast<Edge*>(oeitr->get())->setErrorMetric(0.0f);
            
            _edgeSet.insert(*oeitr);
        }

        TriangleList::iterator titr_p1, titr_p2;
        
        for(titr_p1 = triangles_p1.begin();
            titr_p1 != triangles_p1.end();
            ++titr_p1)
        {
            removeTriangle(const_cast<Triangle*>(titr_p1->get()));
        }

        for(titr_p2 = triangles_p2.begin();
            titr_p2 != triangles_p2.end();
            ++titr_p2)
        {
            removeTriangle(const_cast<Triangle*>(titr_p2->get()));
        }

        //osg::notify(osg::NOTICE)<<"  pNew="<<pNew<<"\tedge_p1"<<edge_p1.get()<<"\tedge_p2"<<edge_p2.get()<<std::endl;
        for(TriangleSet::iterator teitr=edge->_triangles.begin();
            teitr!=edge->_triangles.end();
            ++teitr)
        {
            Triangle* triangle = const_cast<Triangle*>(teitr->get());
            //osg::notify(osg::NOTICE)<<"  edge removed T("<<triangle->_p1.get()<<"\t"<<triangle->_p2.get()<<"\t"<<triangle->_p3.get()<<")"<<std::endl;
            removeTriangle(triangle);
        }

        LocalEdgeList newEdges;

 
        for(titr_p1 = triangles_p1.begin();
            titr_p1 != triangles_p1.end();
            ++titr_p1)
        {
            Triangle* triangle = const_cast<Triangle*>(titr_p1->get());

            Point* p1 = (triangle->_p1==edge_p1 || triangle->_p1==edge_p2)? pNew : triangle->_p1.get();
            Point* p2 = (triangle->_p2==edge_p1 || triangle->_p2==edge_p2)? pNew : triangle->_p2.get();
            Point* p3 = (triangle->_p3==edge_p1 || triangle->_p3==edge_p2)? pNew : triangle->_p3.get();

            Triangle* newTri = addTriangle(p1,p2,p3);

            if (newTri)
            {
                newEdges.insert(newTri->_e1);
                newEdges.insert(newTri->_e2);
                newEdges.insert(newTri->_e3);
            }
        }


        for(titr_p2 = triangles_p2.begin();
            titr_p2 != triangles_p2.end();
            ++titr_p2)
        {
            Triangle* triangle = const_cast<Triangle*>(titr_p2->get());

            Point* p1 = (triangle->_p1==edge_p1 || triangle->_p1==edge_p2)? pNew : triangle->_p1.get();
            Point* p2 = (triangle->_p2==edge_p1 || triangle->_p2==edge_p2)? pNew : triangle->_p2.get();
            Point* p3 = (triangle->_p3==edge_p1 || triangle->_p3==edge_p2)? pNew : triangle->_p3.get();

            Triangle* newTri = addTriangle(p1,p2,p3);

            if (newTri)
            {
                newEdges.insert(newTri->_e1);
                newEdges.insert(newTri->_e2);
                newEdges.insert(newTri->_e3);
            }
        }



        // osg::notify(osg::NOTICE)<<"Edges to recalibarate "<<newEdges.size()<<std::endl;

        for(LocalEdgeList::iterator itr=newEdges.begin();
            itr!=newEdges.end();
            ++itr)
        {
            //osg::notify(osg::NOTICE)<<"updateErrorMetricForEdge("<<itr->get()<<")"<<std::endl;
            updateErrorMetricForEdge(const_cast<Edge*>(itr->get()));
        }

         return true;
    }

    unsigned int testEdge(Edge* edge)
    {
        unsigned int numErrors = 0;
        for(TriangleSet::iterator teitr=edge->_triangles.begin();
            teitr!=edge->_triangles.end();
            ++teitr)
        {
            Triangle* triangle = const_cast<Triangle*>(teitr->get());
            if (!(triangle->_e1 == edge || triangle->_e2 == edge || triangle->_e3 == edge))
            {
                osg::notify(osg::NOTICE)<<"testEdge("<<edge<<"). triangle != point back to this edge"<<std::endl;
                osg::notify(osg::NOTICE)<<"                     triangle->_e1=="<<triangle->_e1.get()<<std::endl;
                osg::notify(osg::NOTICE)<<"                     triangle->_e2=="<<triangle->_e2.get()<<std::endl;
                osg::notify(osg::NOTICE)<<"                     triangle->_e3=="<<triangle->_e3.get()<<std::endl;
                ++numErrors;
            }
        }
        
        if (edge->_triangles.empty())
        {
            osg::notify(osg::NOTICE)<<"testEdge("<<edge<<")._triangles is empty"<<std::endl;
            ++numErrors;
        }
        return numErrors;
    }

    unsigned int testAllEdges()
    {
        unsigned int numErrors = 0;
        for(EdgeSet::iterator itr = _edgeSet.begin();
            itr!=_edgeSet.end();
            ++itr)
        {
            numErrors += testEdge(const_cast<Edge*>(itr->get()));
        }
        return numErrors;
    }

    unsigned int computeNumBoundaryEdges()
    {
        unsigned int numBoundaryEdges = 0;
        for(EdgeSet::iterator itr = _edgeSet.begin();
            itr!=_edgeSet.end();
            ++itr)
        {
            if ((*itr)->isBoundaryEdge()) ++numBoundaryEdges;
        }
        return numBoundaryEdges;
    }


    Point* addPoint(Triangle* triangle, unsigned int p1)
    {
        return addPoint(triangle,_originalPointList[p1].get());
    }

    Point* addPoint(Triangle* triangle, Point* point)
    {
        
        PointSet::iterator itr = _pointSet.find(point);
        if (itr==_pointSet.end())
        {
            //osg::notify(osg::NOTICE)<<"  addPoint("<<point.get()<<")"<<std::endl;
            _pointSet.insert(point);
        }
        else
        {
            point = const_cast<Point*>(itr->get());
            //osg::notify(osg::NOTICE)<<"  reusePoint("<<point.get()<<")"<<std::endl;
        }

        point->_triangles.insert(triangle);
        
        return point;
    }

    void removePoint(Triangle* triangle, Point* point)
    {
        PointSet::iterator itr = _pointSet.find(point);
        if (itr!=_pointSet.end())
        {
            point->_triangles.erase(triangle);
            
            if (point->_triangles.empty())
            {
                // point no longer in use, so need to delete.
                _pointSet.erase(itr);
            }
        }
        
    }
    
    unsigned int testPoint(Point* point)
    {
        unsigned int numErrors = 0;
        
        for(TriangleSet::iterator itr=point->_triangles.begin();
            itr!=point->_triangles.end();
            ++itr)
        {
            Triangle* triangle = const_cast<Triangle*>(itr->get());
            if (!(triangle->_p1 == point || triangle->_p2 == point || triangle->_p3 == point))
            {
                osg::notify(osg::NOTICE)<<"testPoint("<<point<<") error, triangle "<<triangle<<" does not point back to this point"<<std::endl;
                osg::notify(osg::NOTICE)<<"             triangle->_p1 "<<triangle->_p1.get()<<std::endl;
                osg::notify(osg::NOTICE)<<"             triangle->_p2 "<<triangle->_p2.get()<<std::endl;
                osg::notify(osg::NOTICE)<<"             triangle->_p3 "<<triangle->_p3.get()<<std::endl;
                ++numErrors;
            }
        }
        
        return numErrors;
    }
    
    unsigned int testAllPoints()
    {
        unsigned int numErrors = 0;
        for(PointSet::iterator itr = _pointSet.begin();
            itr!=_pointSet.end();
            ++itr)
        {
            numErrors += testPoint(const_cast<Point*>(itr->get()));
        }
        return numErrors;
    }
    
//protected:

    typedef std::vector< osg::ref_ptr<osg::Array> > ArrayList;

    osg::Geometry*                  _geometry;
    
    EdgeSet                         _edgeSet;
    TriangleSet                     _triangleSet;
    PointSet                        _pointSet;
    PointList                       _originalPointList;
    
};

struct CollectTriangleOperator
{

    CollectTriangleOperator():_ec(0) {}

    void setEdgeCollapse(EdgeCollapse* ec) { _ec = ec; }
    
    EdgeCollapse* _ec;    

    // for use  in the triangle functor.
    inline void operator()(unsigned int p1, unsigned int p2, unsigned int p3)
    {
        _ec->addTriangle(p1,p2,p3);
    }

};

EdgeCollapse::~EdgeCollapse()
{
    for_each(_edgeSet.begin(),_edgeSet.end(),dereference_clear());

    for_each(_triangleSet.begin(),_triangleSet.end(),dereference_clear());
    for_each(_pointSet.begin(),_pointSet.end(),dereference_clear());
    for_each(_originalPointList.begin(),_originalPointList.end(),dereference_clear());
}


typedef osg::TriangleIndexFunctor<CollectTriangleOperator> CollectTriangleIndexFunctor;

class CopyArrayToPointsVisitor : public osg::ArrayVisitor
{
    public:
        CopyArrayToPointsVisitor(EdgeCollapse::PointList& pointList):
            _pointList(pointList) {}
        
        template<class T>
        void copy(T& array)
        {
            if (_pointList.size()!=array.size()) return;
        
            for(unsigned int i=0;i<_pointList.size();++i) 
                _pointList[i]->_attributes.push_back((float)array[i]);  
        }
        
        virtual void apply(osg::Array&) {}
        virtual void apply(osg::ByteArray& array) { copy(array); }
        virtual void apply(osg::ShortArray& array) { copy(array); }
        virtual void apply(osg::IntArray& array) { copy(array); }
        virtual void apply(osg::UByteArray& array) { copy(array); }
        virtual void apply(osg::UShortArray& array) { copy(array); }
        virtual void apply(osg::UIntArray& array) { copy(array); }
        virtual void apply(osg::FloatArray& array) { copy(array); }

        virtual void apply(osg::UByte4Array& array)
        {
            if (_pointList.size()!=array.size()) return;
        
            for(unsigned int i=0;i<_pointList.size();++i) 
            {
                osg::UByte4& value = array[i];
                EdgeCollapse::FloatList& attributes = _pointList[i]->_attributes;
                attributes.push_back((float)value.r());  
                attributes.push_back((float)value.g());  
                attributes.push_back((float)value.b());  
                attributes.push_back((float)value.a());  
            }
        }

        virtual void apply(osg::Vec2Array& array)
        {
            if (_pointList.size()!=array.size()) return;
        
            for(unsigned int i=0;i<_pointList.size();++i) 
            {
                osg::Vec2& value = array[i];
                EdgeCollapse::FloatList& attributes = _pointList[i]->_attributes;
                attributes.push_back(value.x());  
                attributes.push_back(value.y());  
            }
        }

        virtual void apply(osg::Vec3Array& array)
        {
            if (_pointList.size()!=array.size()) return;
        
            for(unsigned int i=0;i<_pointList.size();++i) 
            {
                osg::Vec3& value = array[i];
                EdgeCollapse::FloatList& attributes = _pointList[i]->_attributes;
                attributes.push_back(value.x());  
                attributes.push_back(value.y());  
                attributes.push_back(value.z());  
            }
        }
        
        virtual void apply(osg::Vec4Array& array)
        {
            if (_pointList.size()!=array.size()) return;
        
            for(unsigned int i=0;i<_pointList.size();++i) 
            {
                osg::Vec4& value = array[i];
                EdgeCollapse::FloatList& attributes = _pointList[i]->_attributes;
                attributes.push_back(value.x());  
                attributes.push_back(value.y());  
                attributes.push_back(value.z());  
                attributes.push_back(value.w());  
            }
        }
        
        EdgeCollapse::PointList& _pointList;
};

class CopyVertexArrayToPointsVisitor : public osg::ArrayVisitor
{
    public:
        CopyVertexArrayToPointsVisitor(EdgeCollapse::PointList& pointList):
            _pointList(pointList) {}
        
        virtual void apply(osg::Vec2Array& array)
        {
            if (_pointList.size()!=array.size()) return;
        
            for(unsigned int i=0;i<_pointList.size();++i) 
            {
                _pointList[i] = new EdgeCollapse::Point;
                _pointList[i]->_index = i;
                
                osg::Vec2& value = array[i];
                osg::Vec3& vertex = _pointList[i]->_vertex;
                vertex.set(value.x(),value.y(),0.0f);  
            }
        }

        virtual void apply(osg::Vec3Array& array)
        {
            if (_pointList.size()!=array.size()) return;
        
            for(unsigned int i=0;i<_pointList.size();++i) 
            {
                _pointList[i] = new EdgeCollapse::Point;
                _pointList[i]->_index = i;
                
                _pointList[i]->_vertex = array[i];
            }
        }
        
        virtual void apply(osg::Vec4Array& array)
        {
            if (_pointList.size()!=array.size()) return;
        
            for(unsigned int i=0;i<_pointList.size();++i) 
            {
                _pointList[i] = new EdgeCollapse::Point;
                _pointList[i]->_index = i;
                
                osg::Vec4& value = array[i];
                osg::Vec3& vertex = _pointList[i]->_vertex;
                vertex.set(value.x()/value.w(),value.y()/value.w(),value.z()/value.w());  
            }
        }
        
        EdgeCollapse::PointList& _pointList;
};

void EdgeCollapse::setGeometry(osg::Geometry* geometry, const Simplifier::IndexList& protectedPoints)
{
    _geometry = geometry;
    
    // check to see if vertex attributes indices exists, if so expand them to remove them
    if (_geometry->suitableForOptimization())
    {
        // removing coord indices
        osg::notify(osg::INFO)<<"EdgeCollapse::setGeometry(..): Removing attribute indices"<<std::endl;
        _geometry->copyToAndOptimize(*_geometry);
    }

    unsigned int numVertices = geometry->getVertexArray()->getNumElements();
        
    _originalPointList.resize(numVertices);
    
    // copy vertices across to local point list
    CopyVertexArrayToPointsVisitor copyVertexArrayToPoints(_originalPointList);
    _geometry->getVertexArray()->accept(copyVertexArrayToPoints);
    
    // copy other per vertex attributes across to local point list.
    CopyArrayToPointsVisitor        copyArrayToPoints(_originalPointList);

    for(unsigned int ti=0;ti<_geometry->getNumTexCoordArrays();++ti)
    {
        if (_geometry->getTexCoordArray(ti))
            geometry->getTexCoordArray(ti)->accept(copyArrayToPoints);
    }

    if (_geometry->getNormalArray() && _geometry->getNormalBinding()==osg::Geometry::BIND_PER_VERTEX)
        geometry->getNormalArray()->accept(copyArrayToPoints);
        
    if (_geometry->getColorArray() && _geometry->getColorBinding()==osg::Geometry::BIND_PER_VERTEX)
        geometry->getColorArray()->accept(copyArrayToPoints);
        
    if (_geometry->getSecondaryColorArray() && _geometry->getSecondaryColorBinding()==osg::Geometry::BIND_PER_VERTEX)
        geometry->getSecondaryColorArray()->accept(copyArrayToPoints);

    if (_geometry->getFogCoordArray() && _geometry->getFogCoordBinding()==osg::Geometry::BIND_PER_VERTEX)
        geometry->getFogCoordArray()->accept(copyArrayToPoints);

    for(unsigned int vi=0;vi<_geometry->getNumVertexAttribArrays();++vi)
    {
        if (_geometry->getVertexAttribArray(vi) &&  _geometry->getVertexAttribBinding(vi)==osg::Geometry::BIND_PER_VERTEX)
            geometry->getVertexAttribArray(vi)->accept(copyArrayToPoints);
    }

    // now set the protected points up.
    for(Simplifier::IndexList::const_iterator pitr=protectedPoints.begin();
        pitr!=protectedPoints.end();
        ++pitr)
    {
        _originalPointList[*pitr]->_protected = true;
    }


    CollectTriangleIndexFunctor collectTriangles;
    collectTriangles.setEdgeCollapse(this);
    
    _geometry->accept(collectTriangles);
    
}
 


class CopyPointsToArrayVisitor : public osg::ArrayVisitor
{
    public:
        CopyPointsToArrayVisitor(EdgeCollapse::PointList& pointList):
            _pointList(pointList),
            _index(0) {}
        
        template<typename T,typename R>
        void copy(T& array)
        {
            array.resize(_pointList.size());
        
            for(unsigned int i=0;i<_pointList.size();++i) 
            {
                if (_index<_pointList[i]->_attributes.size()) 
                {
                    float val = (_pointList[i]->_attributes[_index]);
                    array[i] = R (val);
                }
            }
                
            ++_index;
        }
        
        virtual void apply(osg::Array&) {}
        virtual void apply(osg::ByteArray& array) { copy<osg::ByteArray,char>(array); }
        virtual void apply(osg::ShortArray& array) { copy<osg::ShortArray,short>(array); }
        virtual void apply(osg::IntArray& array) { copy<osg::IntArray,int>(array); }
        virtual void apply(osg::UByteArray& array) { copy<osg::UByteArray,unsigned char>(array); }
        virtual void apply(osg::UShortArray& array) { copy<osg::UShortArray,unsigned short>(array); }
        virtual void apply(osg::UIntArray& array) { copy<osg::UIntArray,unsigned int>(array); }
        virtual void apply(osg::FloatArray& array) { copy<osg::FloatArray,float>(array); }

        virtual void apply(osg::UByte4Array& array)
        {
            array.resize(_pointList.size());
        
            for(unsigned int i=0;i<_pointList.size();++i) 
            {
                EdgeCollapse::FloatList& attributes = _pointList[i]->_attributes;
                array[i].set((unsigned char)attributes[_index],
                             (unsigned char)attributes[_index+1],
                             (unsigned char)attributes[_index+2],
                             (unsigned char)attributes[_index+3]);
            }
            _index += 4;
        }

        virtual void apply(osg::Vec2Array& array)
        {
            array.resize(_pointList.size());
        
            for(unsigned int i=0;i<_pointList.size();++i) 
            {
                EdgeCollapse::FloatList& attributes = _pointList[i]->_attributes;
                if (_index+1<attributes.size()) array[i].set(attributes[_index],attributes[_index+1]);
            }
            _index += 2;
        }

        virtual void apply(osg::Vec3Array& array)
        {
            array.resize(_pointList.size());
        
            for(unsigned int i=0;i<_pointList.size();++i) 
            {
                EdgeCollapse::FloatList& attributes = _pointList[i]->_attributes;
                if (_index+2<attributes.size()) array[i].set(attributes[_index],attributes[_index+1],attributes[_index+2]);
            }
            _index += 3;
        }
        
        virtual void apply(osg::Vec4Array& array)
        {
            array.resize(_pointList.size());
        
            for(unsigned int i=0;i<_pointList.size();++i) 
            {
                EdgeCollapse::FloatList& attributes = _pointList[i]->_attributes;
                if (_index+3<attributes.size()) array[i].set(attributes[_index],attributes[_index+1],attributes[_index+2],attributes[_index+3]);
            }
            _index += 4;
        }
        
        EdgeCollapse::PointList& _pointList;
        unsigned int _index;
};

class NormalizeArrayVisitor : public osg::ArrayVisitor
{
    public:
        NormalizeArrayVisitor() {}
        
        template<typename Itr>
        void normalize(Itr begin, Itr end)
        {
            for(Itr itr = begin;
                itr != end;
                ++itr)
            {
                itr->normalize();
            }
        }
        
        virtual void apply(osg::Vec2Array& array) { normalize(array.begin(),array.end()); }
        virtual void apply(osg::Vec3Array& array) { normalize(array.begin(),array.end()); }
        virtual void apply(osg::Vec4Array& array) { normalize(array.begin(),array.end()); }
        
};

class CopyPointsToVertexArrayVisitor : public osg::ArrayVisitor
{
    public:
        CopyPointsToVertexArrayVisitor(EdgeCollapse::PointList& pointList):
            _pointList(pointList) {}
        
        virtual void apply(osg::Vec2Array& array)
        {
            array.resize(_pointList.size());
            
            for(unsigned int i=0;i<_pointList.size();++i) 
            {
                _pointList[i]->_index = i;
                osg::Vec3& vertex = _pointList[i]->_vertex;
                array[i].set(vertex.x(),vertex.y());
            }
        }

        virtual void apply(osg::Vec3Array& array)
        {
            array.resize(_pointList.size());
        
            for(unsigned int i=0;i<_pointList.size();++i) 
            {
                _pointList[i]->_index = i;
                array[i] = _pointList[i]->_vertex;
            }
        }
        
        virtual void apply(osg::Vec4Array& array)
        {
            array.resize(_pointList.size());
        
            for(unsigned int i=0;i<_pointList.size();++i) 
            {
                _pointList[i]->_index = i;
                osg::Vec3& vertex = _pointList[i]->_vertex;
                array[i].set(vertex.x(),vertex.y(),vertex.z(),1.0f);
            }
        }
        
        EdgeCollapse::PointList& _pointList;
};


void EdgeCollapse::copyBackToGeometry()
{

    // rebuild the _pointList from the _pointSet
    _originalPointList.clear();
    std::copy(_pointSet.begin(),_pointSet.end(),std::back_inserter(_originalPointList));

    // copy vertices across to local point list
    CopyPointsToVertexArrayVisitor copyVertexArrayToPoints(_originalPointList);
    _geometry->getVertexArray()->accept(copyVertexArrayToPoints);
    
    // copy other per vertex attributes across to local point list.
    CopyPointsToArrayVisitor  copyArrayToPoints(_originalPointList);

    for(unsigned int ti=0;ti<_geometry->getNumTexCoordArrays();++ti)
    {
        if (_geometry->getTexCoordArray(ti))
            _geometry->getTexCoordArray(ti)->accept(copyArrayToPoints);
    }

    if (_geometry->getNormalArray() && _geometry->getNormalBinding()==osg::Geometry::BIND_PER_VERTEX)
    {
        _geometry->getNormalArray()->accept(copyArrayToPoints);

        // now normalize the normals.
        NormalizeArrayVisitor nav;
        _geometry->getNormalArray()->accept(nav);
    }
        
    if (_geometry->getColorArray() && _geometry->getColorBinding()==osg::Geometry::BIND_PER_VERTEX)
        _geometry->getColorArray()->accept(copyArrayToPoints);
        
    if (_geometry->getSecondaryColorArray() && _geometry->getSecondaryColorBinding()==osg::Geometry::BIND_PER_VERTEX)
        _geometry->getSecondaryColorArray()->accept(copyArrayToPoints);

    if (_geometry->getFogCoordArray() && _geometry->getFogCoordBinding()==osg::Geometry::BIND_PER_VERTEX)
        _geometry->getFogCoordArray()->accept(copyArrayToPoints);

    for(unsigned int vi=0;vi<_geometry->getNumVertexAttribArrays();++vi)
    {
        if (_geometry->getVertexAttribArray(vi) &&  _geometry->getVertexAttribBinding(vi)==osg::Geometry::BIND_PER_VERTEX)
            _geometry->getVertexAttribArray(vi)->accept(copyArrayToPoints);
    }

    osg::DrawElementsUInt* primitives = new osg::DrawElementsUInt(GL_TRIANGLES,_triangleSet.size()*3);
    unsigned int pos = 0;
    for(TriangleSet::iterator titr=_triangleSet.begin();
        titr!=_triangleSet.end();
        ++titr)
    {
        const Triangle* triangle = (*titr).get();
        (*primitives)[pos++] = triangle->_p1->_index;
        (*primitives)[pos++] = triangle->_p2->_index;
        (*primitives)[pos++] = triangle->_p3->_index;
    }
    
    _geometry->getPrimitiveSetList().clear();
    _geometry->addPrimitiveSet(primitives);

#if 0    
    osgUtil::SmoothingVisitor::smooth(*_geometry);
    
    osgUtil::TriStripVisitor stripper;
    stripper.stripify(*_geometry);
#endif  
}


Simplifier::Simplifier(float sampleRatio, float maximumError):
            osg::NodeVisitor(osg::NodeVisitor::TRAVERSE_ALL_CHILDREN),
            _sampleRatio(sampleRatio),
            _maximumError(maximumError)
{
}

void Simplifier::simplify(osg::Geometry& geometry)
{
    // pass an empty list of indices to simply(Geometry,IndexList)
    // so that this one method handle both cases of non protected indices
    // and specified indices.
    IndexList emptyList;
    simplify(geometry,emptyList);
}

void Simplifier::simplify(osg::Geometry& geometry, const IndexList& protectedPoints)
{
    osg::notify(osg::WARN)<<"++++++++++++++simplifier************"<<std::endl;

    EdgeCollapse ec;
    ec.setGeometry(&geometry, protectedPoints);

    ec.updateErrorMetricForAllEdges();

    unsigned int numOriginalPrimitives = ec._triangleSet.size();

    while (!ec._edgeSet.empty() &&
           continueSimplification((*ec._edgeSet.begin())->getErrorMetric() , numOriginalPrimitives, ec._triangleSet.size()) && 
           ec.collapseMinimumErrorEdge())
    {
       //osg::notify(osg::WARN)<<"   Collapsed edge ec._triangleSet.size()="<<ec._triangleSet.size()<<" error="<<(*ec._edgeSet.begin())->getErrorMetric()<<" vs "<<getMaximumError()<<std::endl;
    }

    osg::notify(osg::INFO)<<"******* AFTER EDGE COLLAPSE *********"<<ec._triangleSet.size()<<std::endl;

    osg::notify(osg::INFO)<<"Number of triangle errors after edge collapse= "<<ec.testAllTriangles()<<std::endl;
    osg::notify(osg::INFO)<<"Number of edge errors before edge collapse= "<<ec.testAllEdges()<<std::endl;
    osg::notify(osg::INFO)<<"Number of point errors after edge collapse= "<<ec.testAllPoints()<<std::endl;
    osg::notify(osg::INFO)<<"Number of triangles= "<<ec._triangleSet.size()<<std::endl;
    osg::notify(osg::INFO)<<"Number of points= "<<ec._pointSet.size()<<std::endl;
    osg::notify(osg::INFO)<<"Number of edges= "<<ec._edgeSet.size()<<std::endl;
    osg::notify(osg::INFO)<<"Number of boundary edges= "<<ec.computeNumBoundaryEdges()<<std::endl;

    osg::notify(osg::WARN)<<std::endl<<"Simplifier, in = "<<numOriginalPrimitives<<"\tout = "<<ec._triangleSet.size()<<"\terror="<<(*ec._edgeSet.begin())->getErrorMetric()<<"\tvs "<<getMaximumError()<<std::endl<<std::endl;
    osg::notify(osg::WARN)<<           "        !ec._edgeSet.empty()  = "<<!ec._edgeSet.empty()<<std::endl;
    osg::notify(osg::WARN)<<           "        continueSimplification(,,)  = "<<continueSimplification((*ec._edgeSet.begin())->getErrorMetric() , numOriginalPrimitives, ec._triangleSet.size())<<std::endl;
    
    ec.copyBackToGeometry();
}
