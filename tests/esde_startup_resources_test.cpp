#include "StartupResources.h"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include <vector>
#include <chrono>
using namespace StartupResources;
static void write(const std::string& p,const std::string& s){std::filesystem::create_directories(std::filesystem::path(p).parent_path());std::ofstream(p)<<s;}
static std::string xml(pugi::xml_document& d){std::ostringstream s;d.save(s);return s.str();}
int main(int argc,char**argv){
 assert(argc==2);const std::string root=std::string(argv[1])+"/",path=root+"ES-DE/themes/test/theme.xml";
 write(path,"<theme><variables><name>${system.name}</name></variables><view name='system'><text>é &amp; test</text></view></theme>");
 bool value=false;assert(!query(path,false,value));begin(root);
 assert(query(path,false,value)&&value);assert(query(path,true,value)&&!value);
 assert(query(root+"ES-DE/themes/test",true,value)&&value);
 assert(!query(root+"ES-DE/gamelists/test.xml",false,value));
 assert(!query("smb://server/share/theme.xml",false,value));
 assert(!query(root+"ES-DE/themes/../settings/x",false,value));
 pugi::xml_document a,b,c;assert(loadXml(a,path));auto original=xml(a);
 a.child("theme").child("view").remove_child("text");
 assert(loadXml(b,path)&&xml(b)==original);assert(state().xmlHits==1);
 // Snapshot only while active. Releases before navigation; refresh/reload sees edits.
 write(path,"<theme><new/></theme>");assert(loadXml(c,path)&&xml(c)==original);
 const auto missing=root+"resources/not-yet.xml";assert(query(missing,false,value)&&!value);
 write(missing,"<theme/>");end();assert(state().stats.empty()&&state().xml.empty()&&state().bytes==0);
 assert(!query(path,false,value));assert(loadXml(c,path)&&xml(c)!=original);
 begin(root);assert(query(missing,false,value)&&value);assert(loadXml(c,path));
 auto f=[&]{for(int i=0;i<200;i++){bool v;assert(query(path,false,v)&&v);pugi::xml_document doc;assert(loadXml(doc,path));}};
 std::thread t1(f),t2(f);t1.join();t2.join();
 write(root+"resources/bad.xml","<broken>");pugi::xml_document bad,baseline;
 auto x=loadXml(bad,root+"resources/bad.xml"),y=baseline.load_file((root+"resources/bad.xml").c_str());
 assert(!x&&x.status==y.status&&x.offset==y.offset);
 for(int i=0;i<8300;i++)assert(query(root+"resources/missing-"+std::to_string(i),false,value)&&!value);
 assert(state().stats.size()==8192);end();begin(root);
 const std::string big="<theme>"+std::string(1100000,'x')+"</theme>";
 write(root+"resources/a.xml",big);write(root+"resources/b.xml",big);
 assert(loadXml(a,root+"resources/a.xml"));assert(loadXml(b,root+"resources/b.xml"));
 assert(state().bytes<=2*1024*1024&&state().xml.size()==1);end();
 puts("PASS: scope, invalidation, XML isolation, concurrency, errors and bounds");
}
