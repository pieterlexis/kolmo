#include "kolmoconf.hh"
#include <stdexcept>
#include <fstream>
#include <iostream>
#include "LuaContext.hpp"
#include <boost/algorithm/string.hpp>
using namespace std;

KolmoConf::KolmoConf()
{
  d_prototypes["bool"]=std::make_unique<KolmoBool>(false);
  d_prototypes["string"]=std::make_unique<KolmoString>("");
  d_prototypes["integer"]=std::make_unique<KolmoInteger>(0);
  d_prototypes["struct"]=std::make_unique<KolmoStruct>();
  d_prototypes["ipendpoint"]=std::make_unique<KolmoIPEndpoint>();
  luaInit();
}

void KolmoStruct::registerBool(const std::string& name, bool value)
{
  d_store[name]=KolmoBool::make(value);
}

void KolmoStruct::registerString(const std::string& name, const std::string& value)
{
  d_store[name]=KolmoString::make(value);
}

void KolmoStruct::registerIPEndpoint(const std::string& name, const std::string& value)
{
  d_store[name]=KolmoIPEndpoint::make(value);
}


std::map<std::string, std::unique_ptr<KolmoVal> > d_prototypes; // XXX NO NO NO, has to be in kolmoconf!
KolmoStruct* KolmoStruct::registerStruct(const std::string& name, const std::string& strname)
{
  auto f = d_prototypes.find(strname);
  if(f == d_prototypes.end())
    throw std::runtime_error("No class named "+name+" found");
  auto s=dynamic_cast<KolmoStruct*>(f->second.get());
  if(!s)
    throw std::runtime_error("requested wrong type for configuration item "+name);
  
  d_store[name]=s->clone();
  return dynamic_cast<KolmoStruct*>(d_store[name].get());
}


std::unique_ptr<KolmoVal> KolmoStruct::clone() const
{
  KolmoStruct* ret = new KolmoStruct; // XXX
  ret->copyInBasics(*this);
  for(const auto& a : d_store) {
    ret->d_store[a.first] = a.second->clone();
  }
  return std::unique_ptr<KolmoVal>(ret);
}


void KolmoStruct::setValueAt(const std::string& name, const std::string& value)
{
  vector<string> split;
  boost::split(split,name,boost::is_any_of("/"));
  auto ptr=this;
  for(auto iter=split.begin(); iter != split.end(); ++iter) {
    if(iter + 1 != split.end()) {
      ptr=dynamic_cast<KolmoStruct*>(ptr->getMember(*iter));
      if(!ptr) {
        throw std::runtime_error("Traversing variable path, hit non-struct");
      }
    }
    else {
      auto rptr=ptr->getMember(*iter);
      rptr->setValue(value);
    }
  }  
}


void KolmoStruct::registerVariableLua(const std::string& name, const std::string& type, std::unordered_map<string, string> attributes)
{
  auto f = d_prototypes.find(type);
  if(f == d_prototypes.end())
    throw std::runtime_error("No class named "+type+" found");

  auto thing=f->second->clone();
  auto iter = attributes.find("default");
  if(iter != attributes.end()) {
    thing->setValue(iter->second);
    thing->defaultValue=iter->second;
  }
  iter = attributes.find("runtime");
  if(iter != attributes.end())
    thing->runtime=iter->second=="true";
  iter = attributes.find("description");
  if(iter != attributes.end())
    thing->description=iter->second;

  iter = attributes.find("cmdline");
  if(iter != attributes.end())
    thing->cmdline=iter->second;
  
  iter = attributes.find("unit");
  if(iter != attributes.end())
    thing->unit=iter->second;

  //  cout<<"Registering variable "<<name<<" of type "<<type<<", description="<<thing->description<<endl;
  d_store[name]=thing->clone();
}


void KolmoConf::luaInit()
{
  d_lua=new LuaContext();


  d_lua->writeFunction("getMain", [this](){ return &d_main; });
  d_lua->writeVariable("main", &d_main);
  d_lua->registerFunction("registerVariable", &KolmoStruct::registerVariableLua);
  d_lua->registerFunction("registerBool", &KolmoStruct::registerBool);
  
  d_lua->registerFunction("registerString", &KolmoStruct::registerString);
  d_lua->registerFunction("registerStruct", &KolmoStruct::registerStruct);

  //  d_lua->registerFunction("registerVector", &KolmoStruct::registerVector);

  //  d_lua->registerFunction("create", &KolmoVector::create);
  //  d_lua->registerFunction("append", &KolmoVector::append);
  
  d_lua->registerFunction("getStruct", &KolmoStruct::getStruct);
  d_lua->registerFunction("setString", &KolmoStruct::setString);
  d_lua->registerFunction("setIPEndpoint", &KolmoStruct::setIPEndpoint);
  d_lua->registerFunction("setBool", &KolmoStruct::setBool);

  d_lua->registerFunction("addStringToStruct", &KolmoStruct::addStringToStruct);

  
  d_lua->writeFunction("createClass", [this](const std::string& name,
					     vector<pair<string, vector<pair<int, string> > > > i) {
                         //			 cout<<"Registering class "<<name<<endl;
			 KolmoStruct ks;
			 for(const auto& e : i) {
			   cout<<" Attribute '"<<e.first<<"' of type '"<<e.second[0].second<<"' and default value: '"<<e.second[1].second<<"'"<<endl;
			   if(e.second[0].second=="bool")
			     ks.registerBool(e.first, e.second[1].second=="true");
			   else if(e.second[0].second=="string")
			     ks.registerString(e.first, e.second[1].second);
                           else if(e.second[0].second=="struct")
			     ks.registerStruct(e.first, e.second[1].second);
                           else if(e.second[0].second=="ipendpoint")
			     ks.registerIPEndpoint(e.first, e.second[1].second);
			   else {
			     throw std::runtime_error("attempting to register unknown type "+e.second[0].second);
			   }

			 }
			 d_prototypes[name]=ks.clone();
                         return (KolmoStruct*)d_prototypes[name].get();
		       });


  
}

KolmoConf::~KolmoConf()
{
  delete d_lua;
}

void KolmoConf::initSchemaFromString(const std::string& str)
{
  d_lua->executeCode(str);
}

void KolmoConf::initSchemaFromFile(const std::string& str)
try
{
  std::ifstream ifs{str};
  d_lua->executeCode(ifs);
}
catch(const LuaContext::ExecutionErrorException& e) {
  std::cerr << e.what(); 
  try {
    std::rethrow_if_nested(e);
    
    std::cerr << std::endl;
    abort();
  } catch(const std::exception& ne) {
    // ne is the exception that was thrown from inside the lambda
    std::cerr << ": " << ne.what() << std::endl;
    abort();
  }
}

void KolmoConf::initConfigFromCmdline(int argc, char** argv)
{
  for(int n=1; n < argc; ++n) {
    for(const auto m : d_main.getAll()) {
      if(argv[n]==m.second->cmdline) {
        cerr<<"Hit!"<<endl;
        auto ptr = dynamic_cast<KolmoBool*>(m.second);
        if(ptr)
          ptr->setBool(true);
      }
    }
  } 
}

void KolmoConf::initConfigFromFile(const std::string& str)
try
{
  std::ifstream ifs{str};
  d_lua->executeCode(ifs);
}
catch(const LuaContext::ExecutionErrorException& e) {
  std::cerr << e.what(); 
  try {
    std::rethrow_if_nested(e);
    
    std::cerr << std::endl;
  } catch(const std::exception& ne) {
    // ne is the exception that was thrown from inside the lambda
    std::cerr << ": " << ne.what() << std::endl;
  }
  abort();
}

bool KolmoStruct::getBool(const std::string& name)
{
  auto f=d_store.find(name);
  if(f==d_store.end())
    throw std::runtime_error("requested non-existent configuration item "+name);
  auto s=dynamic_cast<KolmoBool*>(f->second.get());
  if(!s)
    throw std::runtime_error("requested wrong type for configuration item "+name);
  return s->getBool();
}

string KolmoStruct::getString(const std::string& name)
{
  auto f=d_store.find(name);
  if(f==d_store.end())
    throw std::runtime_error("requested non-existent configuration item "+name);
  auto s=dynamic_cast<KolmoString*>(f->second.get());
  if(!s)
    throw std::runtime_error("requested wrong type for configuration item "+name);
  return s->d_v;
}


void KolmoStruct::setString(const std::string& name, const std::string& val)
{
  auto f=d_store.find(name);
  if(f==d_store.end())
    throw std::runtime_error("requested non-existent configuration item "+name);
  auto s=dynamic_cast<KolmoString*>(f->second.get());
  if(!s)
    throw std::runtime_error("requested wrong type for configuration item "+name);
  s->d_v = val;
}

void KolmoStruct::addStringToStruct(const std::string& name, const std::string& val)
{
  auto f=d_store.find(name);
  if(f==d_store.end())
    throw std::runtime_error("requested non-existent configuration item "+name);
  auto s=dynamic_cast<KolmoStruct*>(f->second.get());
  if(!s)
    throw std::runtime_error("requested wrong type struct for configuration item "+name);
  //  cout<<"Adding to struct called "<<name<<", description: '"<<s->description<<"'"<<endl;
  s->d_store[std::to_string(s->d_store.size())]=std::make_unique<KolmoString>(val);
}

void KolmoStruct::setBool(const std::string& name, bool val)
{
  auto f=d_store.find(name);
  if(f==d_store.end())
    throw std::runtime_error("requested non-existent configuration item "+name);
  auto s=dynamic_cast<KolmoBool*>(f->second.get());
  if(!s)
    throw std::runtime_error("requested wrong type for configuration item "+name);
  s->setBool(val);
}

void KolmoStruct::setIPEndpoint(const std::string& name, const std::string& in)
{
  auto f=d_store.find(name);
  if(f==d_store.end())
    throw std::runtime_error("requested non-existent configuration item "+name);
  auto s=dynamic_cast<KolmoIPEndpoint*>(f->second.get());
  if(!s)
    throw std::runtime_error("requested wrong type for configuration item "+name);
  s->setIPEndpoint(ComboAddress(in));
}



KolmoStruct* KolmoStruct::getStruct(const std::string& name)
{
  auto f=d_store.find(name);
  if(f==d_store.end())
    throw std::runtime_error("requested non-existent configuration item "+name);
  auto s=dynamic_cast<KolmoStruct*>(f->second.get());
  if(!s)
    throw std::runtime_error("requested wrong type for configuration item "+name);
  return s;
}

KolmoVal* KolmoStruct::getMember(const std::string& name) const
{
  auto f=d_store.find(name);
  if(f==d_store.end())
    throw std::runtime_error("requested non-existent configuration item "+name);
  return f->second.get();
}

void KolmoStruct::tieBool(const std::string& name, std::atomic<bool>* target)
{
  auto ptr = dynamic_cast<KolmoBool*>(getMember(name));
  if(!ptr)
    throw std::runtime_error("Requested bool, but did not get one");

  ptr->tie([target](auto kv) {
      *target=dynamic_cast<const KolmoBool*>(kv)->getBool();
    });
}
