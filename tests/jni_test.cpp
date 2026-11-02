// Compile the actual hook bodies so regressions exercise their control flow.
#include "binder_hook.cpp"
// Avoid collisions between file-local helper names in the included units.
#define clear_jni_exception reflection_clear_jni_exception
#include "reflection_filter.cpp"
#undef clear_jni_exception
#include "resource_hook.cpp"
#include <cassert>
#include <cstdarg>
#include <iostream>
#include <map>
#include <memory>
#include <vector>
void log_info(const char *, ...) {}
void log_error(const char *, ...) {}

namespace mock {
struct String : _jstring { std::u16string data; explicit String(std::u16string s):data(std::move(s)){} };
std::vector<std::unique_ptr<String>> strings;
jstring string(const std::u16string &s) { strings.push_back(std::make_unique<String>(s)); return strings.back().get(); }
jstring string(const char *s) { return string(std::u16string(s, s + std::strlen(s))); }
struct Parcel : _jobject {
    jint size = 0, pos = 0;
    bool owned = true, detach_fails = false;
    int detaches = 0, writes = 0;
    std::map<int, jint> ints;
    std::map<int, jstring> names;
    void integer(int where, jint value) { ints[where] = value; size = std::max(size, where + 4); }
    int name(int where, jstring value, bool utf8 = false) {
        names[where] = value;
        auto length = static_cast<String *>(value)->data.size();
        integer(where, length);
        int end = where + 4 + static_cast<int>(((utf8 ? 1 : 2) * (length + 1) + 3) & ~3);
        size = std::max(size, end);
        return end;
    }
};
std::vector<std::unique_ptr<Parcel>> parcels;
bool exception = false;
bool region_fails = false;
size_t exception_checks = 0, region_reads = 0;
_jthrowable failure;
std::vector<jobject> requests;
constexpr intptr_t Size=1, Position=2, SetPosition=3, ReadString=4, WriteString=5, ReadInt=6,
    Obtain=7, Append=8, Recycle=9, SetSize=10, ReadBinder=11, ReadString8=12, WriteString8=13;
jmethodID method(intptr_t id) { return reinterpret_cast<jmethodID>(id); }
jint call_int(JNIEnv *, jobject object, jmethodID method, va_list) {
    auto &p=*static_cast<Parcel *>(object);
    switch (reinterpret_cast<intptr_t>(method)) {
        case Size: return p.size;
        case Position: return p.pos;
        case ReadInt: { int pos=p.pos; p.pos+=4; return p.ints.contains(pos) ? p.ints.at(pos) : 0; }
        default: assert(false); return 0;
    }
}
void call_void(JNIEnv *, jobject object, jmethodID method, va_list args) {
    auto &p=*static_cast<Parcel *>(object);
    switch (reinterpret_cast<intptr_t>(method)) {
        case SetSize: {
            assert(va_arg(args, jint) == p.size);
            ++p.detaches;
            if (p.detach_fails) exception=true; else p.owned=true;
            return;
        }
        case SetPosition: p.pos=va_arg(args, jint); return;
        case WriteString:
        case WriteString8: {
            assert(p.owned); // would catch the old write into a Binder-owned reply
            ++p.writes;
            p.pos=p.name(p.pos, va_arg(args, jstring), reinterpret_cast<intptr_t>(method)==WriteString8); return;
        }
        case Append: {
            auto *source=static_cast<Parcel *>(va_arg(args, jobject));
            assert(va_arg(args, jint)==0); assert(va_arg(args, jint)==source->size);
            p.ints=source->ints; p.names=source->names; p.size=source->size; p.pos=p.size; return;
        }
        case Recycle: return;
        default: assert(false);
    }
}
jobject call_object(JNIEnv *env, jobject object, jmethodID method, va_list) {
    auto &p=*static_cast<Parcel *>(object);
    auto id=reinterpret_cast<intptr_t>(method);
    if (id==ReadBinder) { p.pos+=24; return nullptr; }
    assert(id==ReadString || id==ReadString8);
    auto name=p.names.find(p.pos);
    if (name==p.names.end()) { exception=true; return nullptr; }
    auto s=name->second;
    auto length=static_cast<String *>(s)->data.size();
    p.pos += 4 + static_cast<int>(((id==ReadString8 ? 1 : 2) * (length+1)+3)&~3);
    // Reading a Parcel while inspecting a request passes through the same hook.
    return filter_feature_read(env,s);
}
jobject call_static_object(JNIEnv *, jclass, jmethodID method, va_list) {
    assert(reinterpret_cast<intptr_t>(method)==Obtain);
    parcels.push_back(std::make_unique<Parcel>()); return parcels.back().get();
}
JNINativeInterface make_table() {
    JNINativeInterface t{};
    t.ExceptionCheck=[](JNIEnv *)->jboolean{++exception_checks; return exception;};
    t.ExceptionOccurred=[](JNIEnv *)->jthrowable{return exception ? &failure : nullptr;};
    t.ExceptionClear=[](JNIEnv *){exception=false;};
    t.Throw=[](JNIEnv *, jthrowable)->jint{exception=true;return 0;};
    t.PushLocalFrame=[](JNIEnv *, jint)->jint{return 0;};
    t.PopLocalFrame=[](JNIEnv *, jobject o)->jobject{return o;};
    t.DeleteLocalRef=[](JNIEnv *, jobject){};
    t.GetStringLength=[](JNIEnv *, jstring s)->jsize{return static_cast<String *>(s)->data.size();};
    t.GetStringRegion=[](JNIEnv *, jstring s, jsize start, jsize length, jchar *out){
        ++region_reads;
        if (region_fails) { exception=true; return; }
        const auto &data=static_cast<String *>(s)->data;
        assert(start>=0 && length>=0 && static_cast<size_t>(start+length)<=data.size());
        std::copy_n(data.data()+start, length, out);
    };
    t.NewString=[](JNIEnv *,const jchar*s,jsize n)->jstring{
        return string(std::u16string(reinterpret_cast<const char16_t*>(s),n));};
    t.CallIntMethodV=call_int; t.CallVoidMethodV=call_void; t.CallObjectMethodV=call_object;
    t.CallStaticObjectMethodV=call_static_object;
    t.IsSameObject=[](JNIEnv *, jobject a, jobject b)->jboolean{return a==b;};
    return t;
}
jboolean transact(JNIEnv *, jobject, jint, jobject data, jobject, jint) {
    requests.push_back(data); return JNI_TRUE;
}
void methods() {
    g_parcel_methods.data_size=method(Size); g_parcel_methods.set_data_size=method(SetSize);
    g_parcel_methods.data_position=method(Position); g_parcel_methods.set_data_position=method(SetPosition);
    g_parcel_methods.read_string=method(ReadString); g_parcel_methods.write_string=method(WriteString);
    g_parcel_methods.read_binder=method(ReadBinder);
    g_parcel_methods.read_string8=method(ReadString8); g_parcel_methods.write_string8=method(WriteString8);
    g_parcel_methods.read_int=method(ReadInt); g_parcel_methods.obtain=method(Obtain);
    g_parcel_methods.append_from=method(Append); g_parcel_methods.recycle=method(Recycle);
    g_parcel_methods_ready.store(true); g_sdk_int.store(36);
}
zygisk::Api *captured_api;
class Capture : public zygisk::ModuleBase { public: void onLoad(zygisk::Api *api,JNIEnv*) override { captured_api=api; } };
jstring resource_name(JNIEnv*, jclass,jlong,jint) { return string("ordinary:resource"); }
}
int main() {
    using namespace mock;
    auto table=make_table(); JNIEnv env{&table}; methods();
    auto config=lh::preset("lineage"); lh::set_active_config(config);
    // Bounded string reads preserve exact matching without pinning/UTF copies.
    char bounded[4];
    assert(lh::read_ascii(&env,string("test"),bounded)=="test");
    assert(lh::read_ascii(&env,string("tests"),bounded).empty());
    assert(lh::read_ascii(&env,string(u"t\u00e9st"),bounded)=="t?st");
    assert(lh::read_ascii(&env,nullptr,bounded).empty());
    auto normal_feature=string(std::u16string(config.feature_names.front().size(),'x'));
    auto hidden_feature=string(config.feature_names.front().c_str());
    const auto strings_before=strings.size();
    assert(filter_feature_read(&env,normal_feature)==normal_feature && strings.size()==strings_before);
    assert(filter_feature_read(&env,hidden_feature)!=hidden_feature);
    auto short_name=string("android");
    const auto reads_before=region_reads;
    assert(filter_feature_read(&env,short_name)==short_name);
    assert(!is_lineage_def_package(&env,short_name));
    assert(!is_hidden_field_name(&env,short_name));
    assert(region_reads==reads_before);
    assert(is_lineage_def_package(&env,string("lineageos.platform")));
    assert(is_hidden_field_name(&env,string("LINEAGE_APK_PATH")));
    assert(!is_hidden_field_name(&env,string("LINEAGE_APK_PATX")));
    region_fails=true;
    assert(filter_feature_read(&env,hidden_feature)==hidden_feature && !exception);
    assert(!is_lineage_def_package(&env,string("lineageos.platform")) && !exception);
    assert(!is_hidden_field_name(&env,string("LINEAGE_APK_PATH")) && !exception);
    region_fails=false;
    // Service list: copy on first matched name, retain ordinary strings and byte count.
    Parcel reply; reply.integer(0,0); reply.integer(4,3);
    int end=reply.name(8,string("lineage.test"));
    end=reply.name(end,string("activity")); reply.name(end,string("profile"));
    reply.owned=false; const auto original_size=reply.size;
    filter_list_reply(&env,&reply);
    assert(!exception && reply.pos==0 && reply.size==original_size);
    assert(reply.detaches==1 && reply.writes==2);
    assert(static_cast<String*>(reply.names.at(8))->data==u"____________");
    // Allocation failure leaves the original reply unmodified and readable.
    Parcel no_memory; no_memory.integer(0,0); no_memory.integer(4,1);
    no_memory.name(8,string("lineage.test")); no_memory.owned=false; no_memory.detach_fails=true;
    filter_list_reply(&env,&no_memory);
    assert(!exception && no_memory.writes==0 && no_memory.pos==0 && !no_memory.owned);
    // Non-target enumeration must not pay the copy/write cost.
    Parcel ordinary; ordinary.integer(0,0); ordinary.integer(4,1); ordinary.name(8,string("activity")); ordinary.owned=false;
    filter_list_reply(&env,&ordinary); assert(ordinary.detaches==0 && ordinary.writes==0);
    // Interface-local transaction IDs can collide: PM must not take the SM path.
    g_service_transactions.get_service=4; g_service_transactions.list_services=4;
    g_package_transactions.has_system_feature=4;
    g_original_transact_native=transact;
    Parcel request; auto namepos=request.name(0,string(kIPackageManagerDescriptor));
    request.name(namepos,string("org.lineageos.trust"));
    auto feature_name=request.names.at(namepos);
    hook_transact_native(&env,nullptr,4,&request,&ordinary,0);
    assert(requests.size()==1 && requests.back()!=&request);
    assert(static_cast<Parcel*>(requests.back())->names.at(namepos)!=feature_name);
    assert(request.names.at(namepos)==feature_name && request.writes==0 && request.pos==0);
    assert(ordinary.writes==0 && ordinary.detaches==0);
    assert(!g_internal_parcel_read); // scope restored after nested inspection
    // Broadcasts: only Intent.action is eligible; matching feature IDs and extras survive.
    const auto action=config.broadcast_actions.front();
    g_activity_transactions.broadcast_intent=4;
    g_activity_transactions.broadcast_intent_with_feature=5;
    for (bool utf8 : {false,true}) for (bool feature : {false,true}) {
        g_sdk_int.store(utf8 ? 36 : 29);
        Parcel broadcast;
        int args=broadcast.name(0,string(kActivityManagerDescriptor));
        int at=args+24; // IApplicationThread binder object, including null binder
        if (feature) at=broadcast.name(at,string(action.c_str()));
        broadcast.integer(at,1); // non-null typed Intent
        const int actionpos=at+4;
        const int extrapos=broadcast.name(actionpos,string(action.c_str()),utf8);
        broadcast.name(extrapos,string(action.c_str()),utf8);
        const auto extra=broadcast.names.at(extrapos), original=broadcast.names.at(actionpos);
        hook_transact_native(&env,nullptr,feature ? 5 : 4,&broadcast,nullptr,0);
        auto *copy=static_cast<Parcel*>(requests.back());
        assert(copy!=&broadcast && copy->writes==1 && copy->size==broadcast.size);
        assert(copy->names.at(actionpos)!=original && copy->names.at(extrapos)==extra);
        if(feature) assert(copy->names.at(args+24)==broadcast.names.at(args+24));
        assert(broadcast.writes==0 && broadcast.pos==0 && !exception && !g_internal_parcel_read);
        // A normal action does not allocate/copy, even with a protected name in extras.
        broadcast.name(actionpos,string(std::u16string(action.size(),'x')),utf8);
        const auto allocations=parcels.size();
        hook_transact_native(&env,nullptr,feature ? 5 : 4,&broadcast,nullptr,0);
        assert(requests.back()==&broadcast && parcels.size()==allocations && !exception);
        broadcast.integer(at,0); // null Intent must be left to the real service
        hook_transact_native(&env,nullptr,feature ? 5 : 4,&broadcast,nullptr,0);
        assert(requests.back()==&broadcast && broadcast.pos==0 && !exception);
    }
    // Unrelated IDs and one-way calls bypass even JNI exception/string checks.
    const auto checks_before=exception_checks;
    const auto regions_before=region_reads;
    hook_transact_native(&env,nullptr,999,&request,nullptr,0);
    assert(requests.back()==&request);
    hook_transact_native(&env,nullptr,4,&request,nullptr,TF_ONE_WAY);
    assert(requests.back()==&request);
    assert(exception_checks==checks_before && region_reads==regions_before);
    // A colliding but unrelated interface is also untouched.
    Parcel unrelated_request;
    unrelated_request.name(0,string("android.test.Unrelated"));
    hook_transact_native(&env,nullptr,4,&unrelated_request,nullptr,0);
    assert(requests.back()==&unrelated_request && unrelated_request.pos==0 && !exception);
    // Preserve real exceptions for both candidate and unrelated transactions.
    exception=true;
    hook_transact_native(&env,nullptr,999,&request,nullptr,0);
    assert(exception && requests.back()==&request);
    hook_transact_native(&env,nullptr,4,&request,nullptr,0);
    assert(exception && requests.back()==&request); exception=false;
    // Failed class resolution never makes the reflection filter apply to all classes.
    _jclass unrelated;
    g_asset_manager_class=nullptr; assert(!is_asset_manager_class(&env,&unrelated));
    // Partial resource registration: only nativeGetResourceName exists on this ROM.
    zygisk::internal::api_table api{};
    api.registerModule=[](auto*,auto*){return true;};
    api.hookJniNativeMethods=[](JNIEnv*,const char*,JNINativeMethod *m,int n){
        for(int i=0;i<n;++i) m[i].fnPtr=i==2 ? reinterpret_cast<void*>(resource_name) : nullptr;
    };
    zygisk::internal::entry_impl<Capture>(&api,&env);
    // Empty lists with enabled switches install nothing or inspect no request.
    lh::set_active_config(lh::Config{});
    const auto empty_checks=exception_checks;
    assert(!install_jni_hook(&env,captured_api));
    assert(!install_resource_hook(&env,captured_api));
    hook_transact_native(&env,nullptr,4,&request,nullptr,0);
    assert(requests.back()==&request && exception_checks==empty_checks);
    lh::set_active_config(config);
    assert(install_resource_hook(&env,captured_api));
    assert(hook_get_resource_name(&env,nullptr,0,0x01010000)!=nullptr);
    assert(hook_get_resource_name(&env,nullptr,0,0x3f010000)==nullptr);
    std::cout << "JNI regressions passed: fast bypass, bounded strings, empty rules, owned replies, allocation failure, AIDL collisions, broadcast scope/encodings, partial hooks, reflection scope\n";
}
