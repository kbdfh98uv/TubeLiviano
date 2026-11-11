#include "../src/os_detect.h"
#include <stdio.h>
#include <string.h>

static int checks, failed;
#define CHECK(x) do { ++checks; if (!(x)) { ++failed; printf("FAIL %d: %s\n",__LINE__,#x); } } while (0)

static OsEvidence file_version(DWORD major, DWORD minor, DWORD build)
{
    OsEvidence e;
    memset(&e,0,sizeof(e));
    e.file_version_ok=TRUE; e.file_major=major; e.file_minor=minor; e.file_build=build;
    return e;
}
int main(void)
{
    OsEvidence e;
    OsInfo info;
    e=file_version(5,1,2600);
    CHECK(os_classify(&e,&info)==OS_DECISION_XP && info.xp && info.major==5);
    e=file_version(5,2,3790);
    CHECK(os_classify(&e,&info)==OS_DECISION_XP && info.xp && info.minor==2);
    e=file_version(10,0,19045);
    CHECK(os_classify(&e,&info)==OS_DECISION_MODERN && !info.xp && info.build==19045);
    e=file_version(10,0,22631); e.rtl_version_ok=TRUE; e.rtl_major=5; e.rtl_minor=1;
    CHECK(os_classify(&e,&info)==OS_DECISION_MODERN && !info.xp && info.build==22631);
    e=file_version(10,0,22631); e.rtl_version_ok=TRUE; e.rtl_major=6; e.rtl_minor=1;
    CHECK(os_classify(&e,&info)==OS_DECISION_MODERN && !info.xp);
    e=file_version(6,1,7601); e.modern_api=TRUE; e.rtl_version_ok=TRUE; e.rtl_major=6;
    CHECK(os_classify(&e,&info)==OS_DECISION_MODERN && !info.xp && info.major>=10);
    e=file_version(5,1,2600); e.modern_api=TRUE; e.rtl_version_ok=TRUE; e.rtl_major=5;
    CHECK(os_classify(&e,&info)==OS_DECISION_MODERN && !info.xp && info.major>=10);
    e=file_version(5,1,2600); e.vista_api=TRUE;
    CHECK(os_classify(&e,&info)==OS_DECISION_UNKNOWN && !info.xp);
    e=file_version(5,2,3790); e.rtl_version_ok=TRUE; e.rtl_major=10;
    CHECK(os_classify(&e,&info)==OS_DECISION_UNKNOWN && !info.xp);
    e=file_version(6,0,6002);
    CHECK(os_classify(&e,&info)==OS_DECISION_UNSUPPORTED && !info.xp);
    e=file_version(6,1,7601);
    CHECK(os_classify(&e,&info)==OS_DECISION_UNSUPPORTED && !info.xp);
    e=file_version(6,2,9200);
    CHECK(os_classify(&e,&info)==OS_DECISION_UNSUPPORTED && !info.xp);
    e=file_version(6,3,9600);
    CHECK(os_classify(&e,&info)==OS_DECISION_UNSUPPORTED && !info.xp);
    e=file_version(5,0,2195);
    CHECK(os_classify(&e,&info)==OS_DECISION_UNSUPPORTED && !info.xp);
    memset(&e,0,sizeof(e));
    CHECK(os_classify(&e,&info)==OS_DECISION_UNKNOWN && !info.xp);
    e.rtl_version_ok=TRUE; e.rtl_major=5; e.rtl_minor=1;
    CHECK(os_classify(&e,&info)==OS_DECISION_UNKNOWN && !info.xp);
    e.rtl_major=6; e.rtl_minor=1;
    CHECK(os_classify(&e,&info)==OS_DECISION_UNKNOWN && !info.xp);
    e.rtl_major=10; e.rtl_minor=0; e.rtl_build=19045;
    CHECK(os_classify(&e,&info)==OS_DECISION_MODERN && !info.xp && info.build==19045);
    memset(&e,0,sizeof(e)); e.modern_api=TRUE;
    CHECK(os_classify(&e,&info)==OS_DECISION_MODERN && !info.xp && info.major==10);
    CHECK(os_classify(NULL,&info)==OS_DECISION_UNKNOWN && !info.xp);
    CHECK(os_classify(&e,NULL)==OS_DECISION_UNKNOWN);
#ifdef _WIN32
    {
        WCHAR error[512];
        CHECK(os_detect(&info,error,512));
        CHECK(info.xp || info.major>=10);
        printf("Real detector: %lu.%lu.%lu, XP=%d\n",(unsigned long)info.major,
               (unsigned long)info.minor,(unsigned long)info.build,info.xp);
    }
#endif
    printf("%d OS routing checks; %d failed\n",checks,failed);
    return failed?1:0;
}
