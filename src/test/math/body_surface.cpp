#include "doctest.h"
#include "aubodysurface.h"
#include <cmath>

namespace {
AuPointBody vertex(double x, double y, double z, bool radial=false)
{
    AuPointBody v;
    v.SetCoord(x,y,z);
    v.m_N = radial ? AuPoint(x,y,z) : AuPoint(0,0,1);
    v.m_N.Unit();
    v.m_Tx = float(x); v.m_Ty = float(y);
    v.m_BoneIndex[0] = 3; v.m_BoneWeight[0] = .6f;
    v.m_BoneIndex[1] = 7; v.m_BoneWeight[1] = .4f;
    return v;
}
}

TEST_CASE("body normals follow DNA-scaled geometry without changing skinning")
{
    auto a=vertex(0,0,0),b=vertex(3,0,0),c=vertex(0,1,2);
    // Normal from the unscaled authored surface is deliberately stale.
    for(auto* v:{&a,&b,&c}){v->m_N=AuPoint(0,-1,1);v->m_N.Unit();}
    auto duplicate=a;duplicate.m_Tx=.9f;
    QList<AuTriBody> mesh;
    mesh << AuTriBody(a,b,c) << AuTriBody(duplicate,b,c);
    AuPoint expected(0,-2,1);expected.Unit();
    AuBodySurface::SmoothNormals(mesh);
    REQUIRE(mesh.size()==2);
    for(const auto& t:mesh)
        for(const auto* v:{&t.m_V0,&t.m_V1,&t.m_V2})
        {
            CHECK(v->m_N.Dot(expected)==doctest::Approx(1));
            CHECK(v->m_BoneWeight[0]==doctest::Approx(.6));
            CHECK(v->m_BoneWeight[1]==doctest::Approx(.4));
        }
    CHECK(mesh[1].m_V0.m_Tx==doctest::Approx(.9));
    CHECK(mesh[0].m_V1.x==doctest::Approx(3));
    CHECK(mesh[0].m_V2.z==doctest::Approx(2));
}

TEST_CASE("body refinement preserves a planar UV and skinning seam")
{
    QList<AuTriBody> mesh;
    mesh << AuTriBody(vertex(0,0,0),vertex(1,0,0),vertex(0,1,0));
    mesh << AuTriBody(vertex(1,0,0),vertex(1,1,0),vertex(0,1,0));
    AuBodySurface::Refine(mesh, 2);
    REQUIRE(mesh.size() == 32);
    for(const auto& t : mesh)
        for(const auto* v : {&t.m_V0,&t.m_V1,&t.m_V2})
        {
            CHECK(v->z == doctest::Approx(0));
            CHECK(v->m_Tx == doctest::Approx(v->x));
            CHECK(v->m_Ty == doctest::Approx(v->y));
            CHECK(v->m_N.Dot(v->m_N) == doctest::Approx(1));
            CHECK(v->m_BoneIndex[0] == 3);
            CHECK(v->m_BoneIndex[1] == 7);
            CHECK(v->m_BoneWeight[0] == doctest::Approx(.6));
            CHECK(v->m_BoneWeight[1] == doctest::Approx(.4));
        }
}

TEST_CASE("closed curved body edges are rounded consistently")
{
    AuPointBody p[] = {vertex(1,0,0,true),vertex(-1,0,0,true),vertex(0,1,0,true),
        vertex(0,-1,0,true),vertex(0,0,1,true),vertex(0,0,-1,true)};
    int faces[][3] = {{0,2,4},{2,1,4},{1,3,4},{3,0,4},{2,0,5},{1,2,5},{3,1,5},{0,3,5}};
    QList<AuTriBody> mesh;
    for(const auto& f : faces) mesh << AuTriBody(p[f[0]],p[f[1]],p[f[2]]);
    AuBodySurface::Refine(mesh, 1);
    REQUIRE(mesh.size() == 32);
    int sharedMidpoints = 0;
    for(const auto& t : mesh)
        for(const auto* v : {&t.m_V0,&t.m_V1,&t.m_V2})
            if(v->x > .5 && v->y > .5 && std::fabs(v->z) < 1.e-8)
            {
                CHECK(v->x == doctest::Approx(v->y));
                CHECK(v->x < .7);
                ++sharedMidpoints;
            }
    CHECK(sharedMidpoints == 6);
}

TEST_CASE("mixed skin palette triangles and disabled detail retain topology")
{
    AuTriBody t(vertex(0,0,0),vertex(1,0,0),vertex(0,1,0));
    t.m_V1.m_TopFace = true;
    QList<AuTriBody> mesh; mesh << t;
    AuBodySurface::Refine(mesh, 2);
    REQUIRE(mesh.size() == 1);
    CHECK(mesh[0].m_V1.m_TopFace);
    t.m_V1.m_TopFace = false;
    mesh[0] = t;
    AuBodySurface::Refine(mesh, 0);
    REQUIRE(mesh.size() == 1);
}

TEST_CASE("new vertices retain both sides of a bone transition")
{
    auto a=vertex(0,0,0), b=vertex(1,0,0), c=vertex(0,1,0);
    for(auto* v : {&a,&b,&c})
    {
        v->m_BoneWeight[0]=1;
        v->m_BoneWeight[1]=0;
    }
    b.m_BoneIndex[0]=7;
    QList<AuTriBody> mesh;mesh << AuTriBody(a,b,c);
    AuBodySurface::Refine(mesh,1);
    REQUIRE(mesh.size() == 4);
    const auto& mid=mesh[0].m_V1;
    CHECK(mid.x == doctest::Approx(.5));
    CHECK(mid.m_BoneIndex[0] == 3);
    CHECK(mid.m_BoneIndex[1] == 7);
    CHECK(mid.m_BoneWeight[0] == doctest::Approx(.5));
    CHECK(mid.m_BoneWeight[1] == doctest::Approx(.5));
}
