#include "aubodysurface.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <vector>

namespace {
int detail = 0;
using Key = std::array<long long, 3>;
using Edge = std::pair<Key, Key>;
struct Corner { AuPoint normal, guide; double angle; };
struct EdgeInfo { int count = 0; bool locked = false; AuPoint a, b; };

AuPoint unit(AuPoint p)
{
    if(!p.IsZero()) p.Unit();
    return p;
}

AuPointBody midpoint(const AuPointBody& a, const AuPointBody& b, bool curved)
{
    AuPointBody v = a;
    AuPoint edge = static_cast<const AuPoint&>(b) - static_cast<const AuPoint&>(a);
    AuPoint p = (static_cast<const AuPoint&>(a) + static_cast<const AuPoint&>(b)) * .5;
    if(curved)
    {
        // Midpoint of the cubic PN edge. Bound displacement on malformed or
        // highly deformed parts; shared edges use the same curvature rule.
        AuPoint correction = (b.m_N * edge.Dot(b.m_N) - a.m_N * edge.Dot(a.m_N)) * .125;
        double limit = std::sqrt(edge.Dot(edge)) * .12;
        double length = std::sqrt(correction.Dot(correction));
        if(length > limit && length > 0) correction *= limit / length;
        p += correction;
    }
    v.SetCoord(p);
    v.m_N = unit(a.m_N + b.m_N);
    v.m_Tx = (a.m_Tx + b.m_Tx) * .5f;
    v.m_Ty = (a.m_Ty + b.m_Ty) * .5f;
    v.m_Shine = (a.m_Shine + b.m_Shine) * .5f;
    std::map<int, float> influences;
    for(int i=0; i<4; ++i)
    {
        if(a.m_BoneWeight[i] > 0) influences[a.m_BoneIndex[i]] += a.m_BoneWeight[i] * .5f;
        if(b.m_BoneWeight[i] > 0) influences[b.m_BoneIndex[i]] += b.m_BoneWeight[i] * .5f;
    }
    std::vector<std::pair<int, float>> weights(influences.begin(), influences.end());
    std::sort(weights.begin(), weights.end(), [](const std::pair<int,float>& x, const std::pair<int,float>& y) {
        return x.second == y.second ? x.first < y.first : x.second > y.second;
    });
    float sum = 0;
    for(size_t i=0; i<weights.size() && i<4; ++i) sum += weights[i].second;
    for(int i=0; i<4; ++i)
    {
        v.m_BoneIndex[i] = size_t(i) < weights.size() ? weights[i].first : 0;
        v.m_BoneWeight[i] = size_t(i) < weights.size() && sum > 0 ? weights[i].second / sum : 0;
    }
    return v;
}
}

void AuBodySurface::SetDetail(int levels) { detail = qBound(0, levels, 2); }
int AuBodySurface::Detail() { return detail; }

void AuBodySurface::Refine(QList<AuTriBody>& triangles, int levels)
{
    levels = qBound(0, levels, 2);
    if(!levels || triangles.isEmpty() || triangles.size() > 4000) return;
    double extent = 1.;
    for(const auto& t : triangles)
        for(const auto* v : {&t.m_V0, &t.m_V1, &t.m_V2})
        {
            if(!std::isfinite(v->x) || !std::isfinite(v->y) || !std::isfinite(v->z) ||
                !std::isfinite(v->m_N.x) || !std::isfinite(v->m_N.y) || !std::isfinite(v->m_N.z)) return;
            extent = std::max(extent, std::max(std::fabs(v->x), std::max(std::fabs(v->y), std::fabs(v->z))));
        }
    const double epsilon = extent * 1.e-7;
    auto key = [epsilon](const AuPoint& p) -> Key {
        return {{std::llround(p.x / epsilon), std::llround(p.y / epsilon), std::llround(p.z / epsilon)}};
    };

    // Recompute angle-weighted normals after DNA scaling. Use authored normals
    // as crease guides, and weld shading across UV duplicates without moving
    // their positions. Opposite sides of double-sided parts never mix.
    std::map<Key, std::vector<Corner>> corners;
    for(const auto& t : triangles)
    {
        const AuPointBody* v[] = {&t.m_V0, &t.m_V1, &t.m_V2};
        for(int i=0; i<3; ++i)
        {
            AuPoint e0 = static_cast<const AuPoint&>(*v[(i+1)%3]) - *v[i];
            AuPoint e1 = static_cast<const AuPoint&>(*v[(i+2)%3]) - *v[i];
            AuPoint n = unit(e0.Cross(e1));
            AuPoint guide = unit(v[i]->m_N);
            if(n.IsZero() || guide.IsZero()) continue;
            if(n.Dot(guide) < 0) n *= -1.;
            double angle = std::acos(qBound(-1., unit(e0).Dot(unit(e1)), 1.));
            corners[key(*v[i])].push_back({n, guide, angle});
        }
    }
    for(auto& t : triangles)
        for(auto* v : {&t.m_V0, &t.m_V1, &t.m_V2})
        {
            AuPoint n(0,0,0), guide = unit(v->m_N);
            for(const auto& corner : corners[key(*v)])
                if(corner.guide.Dot(guide) > -.05) n += corner.normal * corner.angle;
            if(!n.IsZero()) v->m_N = unit(n);
        }

    for(int level=0; level<levels && triangles.size() <= 4000; ++level)
    {
        std::map<Edge, EdgeInfo> edges;
        for(const auto& t : triangles)
        {
            const AuPointBody* v[] = {&t.m_V0, &t.m_V1, &t.m_V2};
            bool mixed = v[0]->m_TopFace != v[1]->m_TopFace || v[0]->m_TopFace != v[2]->m_TopFace;
            for(int i=0; i<3; ++i)
            {
                Key a = key(*v[i]), b = key(*v[(i+1)%3]);
                AuPoint na = v[i]->m_N, nb = v[(i+1)%3]->m_N;
                if(b < a) { std::swap(a,b); std::swap(na,nb); }
                auto& e = edges[{a,b}];
                if(e.count && (e.a.Dot(na) < .98 || e.b.Dot(nb) < .98)) e.locked = true;
                e.a = na; e.b = nb; ++e.count;
                e.locked = e.locked || mixed;
            }
        }
        auto mid = [&](const AuPointBody& a, const AuPointBody& b) {
            Key ka = key(a), kb = key(b);
            if(kb < ka) std::swap(ka,kb);
            const auto& e = edges.at({ka,kb});
            // Open seams between separately generated anatomy pieces and hard
            // creases remain on their original edges, avoiding cracks.
            return midpoint(a, b, e.count == 2 && !e.locked && a.m_N.Dot(b.m_N) > -.25);
        };
        QList<AuTriBody> refined;
        refined.reserve(triangles.size() * 4);
        for(const auto& t : triangles)
        {
            if(t.m_V0.m_TopFace != t.m_V1.m_TopFace || t.m_V0.m_TopFace != t.m_V2.m_TopFace)
            {
                // The legacy vertex format stores this blend flag as a bool;
                // retaining these triangles preserves the skin palette blend.
                refined.append(t);
                continue;
            }
            auto ab = mid(t.m_V0,t.m_V1), bc = mid(t.m_V1,t.m_V2), ca = mid(t.m_V2,t.m_V0);
            refined.append(AuTriBody(t.m_V0,ab,ca));
            refined.append(AuTriBody(ab,t.m_V1,bc));
            refined.append(AuTriBody(ca,bc,t.m_V2));
            refined.append(AuTriBody(ab,bc,ca));
        }
        triangles.swap(refined);
    }
}

void AuBodySurface::SmoothNormals(QList<AuTriBody>& triangles)
{
    if(triangles.isEmpty()) return;
    double extent = 1.;
    for(const auto& t : triangles)
        for(const auto* v : {&t.m_V0, &t.m_V1, &t.m_V2})
        {
            if(!std::isfinite(v->x) || !std::isfinite(v->y) || !std::isfinite(v->z) ||
                !std::isfinite(v->m_N.x) || !std::isfinite(v->m_N.y) || !std::isfinite(v->m_N.z)) return;
            extent = std::max(extent, std::max(std::fabs(v->x), std::max(std::fabs(v->y), std::fabs(v->z))));
        }
    const double epsilon = extent * 1.e-7;
    auto key = [epsilon](const AuPoint& p) -> Key {
        return {{std::llround(p.x / epsilon), std::llround(p.y / epsilon), std::llround(p.z / epsilon)}};
    };

    // Recompute angle-weighted normals after DNA scaling. Use authored normals
    // as crease guides, and weld shading across UV duplicates without moving
    // their positions. Opposite sides of double-sided parts never mix.
    std::map<Key, std::vector<Corner>> corners;
    for(const auto& t : triangles)
    {
        const AuPointBody* v[] = {&t.m_V0, &t.m_V1, &t.m_V2};
        for(int i=0; i<3; ++i)
        {
            AuPoint e0 = static_cast<const AuPoint&>(*v[(i+1)%3]) - *v[i];
            AuPoint e1 = static_cast<const AuPoint&>(*v[(i+2)%3]) - *v[i];
            AuPoint n = unit(e0.Cross(e1));
            AuPoint guide = unit(v[i]->m_N);
            if(n.IsZero() || guide.IsZero()) continue;
            if(n.Dot(guide) < 0) n *= -1.;
            double angle = std::acos(qBound(-1., unit(e0).Dot(unit(e1)), 1.));
            corners[key(*v[i])].push_back({n, guide, angle});
        }
    }
    for(auto& t : triangles)
        for(auto* v : {&t.m_V0, &t.m_V1, &t.m_V2})
        {
            AuPoint n(0,0,0), guide = unit(v->m_N);
            for(const auto& corner : corners[key(*v)])
                if(corner.guide.Dot(guide) > -.05) n += corner.normal * corner.angle;
            if(!n.IsZero()) v->m_N = unit(n);
        }

}
