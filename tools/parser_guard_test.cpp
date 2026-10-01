// Verify the mesh/skeleton parsers FAIL CLOSED on malformed headers instead of OOM-ing or hanging.
// Builds tiny crafted blobs with impossible counts / zero stride / cyclic bone links and asserts each
// parse returns false (or a bounded skeleton) quickly, without a huge allocation.
#include "model/MeshParser.h"
#include "model/AstSkeleton.h"
#include "model/ModelGeometry.h"
#include <QByteArray>
#include <QElapsedTimer>
#include <cstring>
#include <cstdio>

static void put8(QByteArray& d, uint8_t v){ d.append(char(v)); }
static void put16(QByteArray& d, uint16_t v){ d.append(char(v&0xff)); d.append(char(v>>8)); }
static void put32(QByteArray& d, uint32_t v){ for(int i=0;i<4;++i) d.append(char((v>>(8*i))&0xff)); }
static void putf(QByteArray& d, float f){ uint32_t v; std::memcpy(&v,&f,4); put32(d,v); }

int main(){
    int fails=0;
    auto req=[&](bool ok,const char* m){ if(!ok){ printf("FAIL: %s\n",m); ++fails; } };
    QElapsedTimer t;

    // 1. v1 .smd claiming tri=0xFFFFFFFF (index alloc would be ~12 GB) — must reject fast.
    {
        QByteArray d;
        put8(d,1);              // version 1
        put32(d,0xFFFFFFFFu);   // tri (huge)
        put32(d,0xFFFFFFFFu);   // nv  (huge)
        put8(d,4); put8(d,1); put8(d,0); put32(d,0);      // b9, meshCount=1, b11, nameBytesTotal
        for(int i=0;i<6;++i) putf(d,0.f);                  // bbox
        put32(d,0); put32(d,0);                            // one mesh header (nameBytes, triStart)
        ModelGeometry g; QString err; t.restart();
        bool ok = MeshParser::parseSmd(d, "bad_v1.smd", g, &err);
        req(!ok, "huge-tri v1 .smd should be rejected");
        req(t.elapsed() < 2000, "huge-tri v1 .smd rejected slowly (possible OOM path)");
        printf("  v1 huge-tri: ok=%d (%lld ms)  err=%s\n", ok, t.elapsed(), qPrintable(err));
    }

    // 2. .fmt v9 with a zero-stride vertex format (0x200) and a large nv — the stride-0 infinite loop.
    {
        QByteArray d;
        put8(d,9);              // version 9
        put16(d,1);             // meshCount echo
        put8(d,0);              // nG
        put16(d,0);             // nP
        put8(d,0);              // flag (0 = no extra block)
        for(int i=0;i<6;++i) putf(d,0.f);                  // bbox → offset now 0x1f
        d.append("DOLm",4);                                 // DOLm tag at 0x1f
        put8(d,4);              // layoutVersion
        put8(d,0);
        put8(d,1);              // lodCount
        put8(d,1);              // meshCount
        put8(d,0);
        put32(d,0x200);         // vertexFormat 0x200 → stride 0
        put32(d,0); put32(d,1000000);   // lod0: tri=0, nv=1,000,000
        put32(d,0); put32(d,0);         // mesh: start=0, count=0 (nidx=0 == tri*3)
        ModelGeometry g; QString err; t.restart();
        bool ok = MeshParser::parseFmt(d, "bad_stride0.fmt", g, &err);
        req(!ok, "zero-stride .fmt with big nv should be rejected");
        req(t.elapsed() < 2000, "zero-stride .fmt rejected slowly (possible infinite append)");
        printf("  fmt stride0/nv=1e6: ok=%d verts=%d (%lld ms)  err=%s\n", ok, g.vertices.size(), t.elapsed(), qPrintable(err));
    }

    // 3. .ast v12 with a bone cycle (bone0.child=1, bone1.child=1) — must not hang / stack-overflow.
    {
        QByteArray d;
        put8(d,12); put8(d,2); put8(d,0); put8(d,0); put8(d,0); put8(d,0); put8(d,0); put8(d,0);
        // bone 0: sibling 255, child 1, identity, name "a"
        put8(d,255); put8(d,1); for(int i=0;i<16;++i) putf(d,(i%5==0)?1.f:0.f); put8(d,1); put8(d,0); d.append("a");
        // bone 1: sibling 255, child 1 (CYCLE — points to itself/back), identity, name "b"
        put8(d,255); put8(d,1); for(int i=0;i<16;++i) putf(d,(i%5==0)?1.f:0.f); put8(d,1); put8(d,0); d.append("b");
        QString err; t.restart();
        AstSkeleton::Skeleton sk = AstSkeleton::parse(d, false, &err);
        req(t.elapsed() < 2000, "cyclic .ast parsed slowly (possible infinite recursion)");
        req(sk.bones.size() == 2, "cyclic .ast should still yield its 2 bones, bounded");
        printf("  ast cycle: bones=%d (%lld ms)\n", sk.bones.size(), t.elapsed());
    }

    // 4. Sanity: a well-formed minimal v12 skeleton still parses (guards didn't break valid input).
    req(AstSkeleton::selfTest().isEmpty(), "AstSkeleton::selfTest regressed");
    req(MeshParser::selfTest().isEmpty(), "MeshParser::selfTest regressed");

    printf(fails? "RESULT: %d FAILURE(S)\n":"RESULT: PASS\n", fails);
    return fails?1:0;
}
