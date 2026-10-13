#include "dc4/system_menu.h"
#include "dc4/mesvm.h"
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>

namespace {
int failures=0;
void check(bool result,const char* expr,int line){if(!result){std::cerr<<line<<": "<<expr<<'\n';++failures;}}
#define CHECK(x) check(bool(x),#x,__LINE__)
using Menu=dc4ui::SystemMenu;
using Page=dc4ui::SystemMenuPage;
using Type=dc4ui::SystemMenuActionType;
std::string lower(std::string text){for(char& c:text)c=char(std::tolower(static_cast<unsigned char>(c)));return text;}
bool has(Menu& m,const std::string& tag){for(const auto& c:m.controls())if(c.tag==tag&&c.enabled)return true;return false;}
dc4ui::SystemMenuAction click(Menu& m,const std::string& tag){
    for(const auto& c:m.controls())if(c.tag==tag&&c.enabled){m.pointer_down(c.x+c.w/2,c.y+c.h/2);auto action=m.pointer_up(c.x+c.w/2,c.y+c.h/2);return m.confirming()?m.answer_confirmation(true):action;}return {};
}
void bmp(const std::filesystem::path& path,const std::vector<uint8_t>& rgba){
    std::ofstream o(path,std::ios::binary);if(!o)return;
    auto u16=[&](uint16_t v){o.put(char(v));o.put(char(v>>8));};
    auto u32=[&](uint32_t v){u16(uint16_t(v));u16(uint16_t(v>>16));};
    o.put('B');o.put('M');u32(54+1280*720*4);u32(0);u32(54);u32(40);u32(1280);u32(uint32_t(-720));u16(1);u16(32);u32(0);u32(1280*720*4);u32(0);u32(0);u32(0);u32(0);
    for(size_t i=0;i<rgba.size();i+=4){o.put(char(rgba[i+2]));o.put(char(rgba[i+1]));o.put(char(rgba[i]));o.put(char(rgba[i+3]));}
}
void profile(){
    const auto path=std::filesystem::temp_directory_path()/"dc4engine-system-menu-test.profile";
    Menu first;
    CHECK(first.config().master_volume==100);
    first.set_variable(99,7,46);first.mark_cg_seen("BG\\DC4_EV_ASA07B.CRX");first.mark_scene_seen("DC4_MID20190301A1.mes");first.mark_music_seen("Music\\001.PCM");
    std::string error;CHECK(first.save_profile(path.string(),&error));
    Menu second;CHECK(second.load_profile(path.string(),&error));CHECK(second.config().music_volume==46);
    CHECK(second.cg_seen("bg/dc4_ev_asa07b"));CHECK(second.scene_seen("dc4_mid20190301a1"));CHECK(second.music_seen("music/001"));
    {std::ofstream bad(path);bad<<"DC4ENGINE_PROFILE 1\nv 99 7 99\nc \"../escape\"\n";}
    CHECK(!second.load_profile(path.string(),&error));CHECK(second.config().music_volume==46);
    std::filesystem::remove(path);
}
// NAMES.DAT is the independent unlock-index -> chapter catalogue. Route menu
// scripts use different local selection indexes, especially outside Asa.
std::vector<std::string> scene_catalogue(const std::string& data){
    std::ifstream in(std::filesystem::path(data)/"GRP/NAMES.DAT",std::ios::binary);
    auto u32=[&](){unsigned char b[4]{};in.read(reinterpret_cast<char*>(b),4);return uint32_t(b[0])|uint32_t(b[1])<<8|uint32_t(b[2])<<16|uint32_t(b[3])<<24;};
    in.seekg(228);
    for(int stride:{48,36,32}){auto count=u32();in.seekg(std::streamoff(count)*stride,std::ios::cur);}
    std::vector<std::string> names;
    auto count=u32();CHECK(count==469);if(count>2000)return names;
    for(uint32_t i=0;i<count;++i){char name[33]{};in.read(name,32);names.emplace_back(name);}
    CHECK(bool(in));return names;
}
void render_replay(const std::string& data,Menu& menu,const dc4ui::SystemMenuAction& selected,const std::string& tag){
    dc4scene::SceneRunner scene(data);
    dc4scene::FontRenderer font;
#ifdef _WIN32
    font.load("C:/Windows/Fonts/msgothic.ttc",0,22);scene.set_font(&font);
#endif
    CHECK(scene.load_sheets(data+"/MES/start.mes"));
    CHECK(scene.load_ui_script(data+"/MES/win_size.mes"));
    CHECK(scene.load_script(data+"/MES/"+selected.asset+".mes",selected.label));
    menu.initialize_scene(scene,true);
    CHECK(scene.variable(9,7)==1);
    bool ready=false;
    for(int yields=0;yields<32;++yields){
        ready=scene.advance_to_next_text();if(ready)break;
        if(!scene.pending_movie().empty())scene.finish_movie();
        else if(scene.waiting_for_time())scene.finish_script_wait();
        else break;
    }
    CHECK(ready);CHECK(!scene.current_line().empty());CHECK(scene.last_error().empty());
    scene.draw(-1);
    if(const char* capture=std::getenv("DC4_MENU_CAPTURES")){
        std::filesystem::create_directories(capture);bmp(std::filesystem::path(capture)/(tag+"-replay.bmp"),scene.frame());
    }
    if(tag=="sce0"){
        int advances=0;
        for(;advances<1000&&!scene.scene_ended()&&scene.last_error().empty();++advances){
            if(!scene.advance_to_next_text())break;
        }
        CHECK(advances<1000);CHECK(scene.scene_ended());CHECK(scene.last_error().empty());
        CHECK(lower(scene.next_script())=="sce_end");
        CHECK(lower(scene.script_name())==lower(selected.asset)+".mes");
    }
    menu.initialize_scene(scene,false);CHECK(scene.variable(9,7)==0);
}
void scene_selections(const std::string& data){
    Menu m;std::string error;CHECK(m.load(data,&error));if(!m.loaded())return;
    auto names=scene_catalogue(data);
    for(const auto& name:names)m.mark_scene_seen(name);
    std::set<int> checked;
    size_t replay_lines=0;
    for(const char* route:{"asa","nin","sor","hyr","sii","miu","tiy","cmn"}){
        m.open(Page::Scenes);m.activate(route);
        bool rendered=false;
        const int pages=m.variable(3,2)+1;
        for(int page=0;page<pages;++page){
            auto controls=m.controls();
            for(const auto& control:controls){
                const auto& tag=control.tag;
                if(!control.enabled||tag.size()<4||tag.rfind("sce",0)!=0||!std::isdigit(static_cast<unsigned char>(tag[3])))continue;
                const int id=std::stoi(tag.substr(3));
                CHECK(id>=0&&size_t(id)<names.size());if(id<0||size_t(id)>=names.size())continue;
                auto selected=click(m,tag);
                if(selected.type!=Type::StartScene||lower(selected.asset)!=names[size_t(id)])
                    std::cerr<<route<<"/"<<page<<" "<<tag<<": expected "<<names[size_t(id)]<<", got "<<selected.asset<<" ("<<m.error()<<")\n";
                CHECK(selected.type==Type::StartScene);CHECK(lower(selected.asset)==names[size_t(id)]);
                checked.insert(id);
                if(selected.type==Type::StartScene){
                    dc4::MesFile mes;
                    CHECK(mes.load_file(data+"/MES/"+selected.asset+".mes",&error));
                    dc4::MesVm vm(mes);vm.write_variable(9,7,1);
                    if(!selected.label.empty())CHECK(vm.jump(selected.label));
                    dc4::MesCommand command;int choices=0;
                    std::string destination;int steps=0,transfers=0;
                    for(;steps<100000;++steps){
                        if(vm.next(&command)){
                            if(command.op==0x6B)++replay_lines;
                            if(command.op==0x4F||command.op==0x50){
                                destination=lower(command.str);
                                if(destination=="sce_end")break;
                                if(++transfers>16)break;
                                if(!mes.load_file(data+"/MES/"+command.str+".mes",&error)||!vm.bind(mes))break;
                            }
                        }else if(vm.waiting_for_choice())CHECK(vm.choose(choices++%vm.choices().size()));
                        else break;
                    }
                    if(!vm.error().empty()||destination!="sce_end")
                        std::cerr<<tag<<" replay: "<<vm.error()<<" destination="<<destination<<'\n';
                    CHECK(vm.error().empty());CHECK(destination=="sce_end");CHECK(steps<100000);
                    // Exercise the same shared startup used by both hosts with
                    // one real rendered chapter per character/common tab.
                    if(!rendered){render_replay(data,m,selected,tag);rendered=true;}
                }
                m.resume_scenes();
                CHECK(m.page()==Page::Scenes);CHECK(m.variable(3,6)==page);CHECK(has(m,tag));
            }
            m.scroll(1);
        }
    }
    CHECK(checked.size()==names.size());
    std::cout<<"Scene Mode selections checked: "<<checked.size()<<", replay dialogue commands: "<<replay_lines<<'\n';
}
void assets(const std::string& data){
    Menu m;std::string error;
    CHECK(m.load(data,&error));if(!m.loaded()){std::cerr<<error<<'\n';return;}
    dc4scene::FontRenderer font;
#ifdef _WIN32
    font.load("C:/Windows/Fonts/msgothic.ttc",0,22);
#endif
    std::vector<uint8_t> frame;
    const char* capture=std::getenv("DC4_MENU_CAPTURES");
    auto shot=[&](const std::string& name){m.draw(-1,-1,false,font,&frame);CHECK(frame.size()==1280u*720u*4u);if(capture){std::filesystem::create_directories(capture);bmp(std::filesystem::path(capture)/(name+".bmp"),frame);}};
    CHECK(has(m,"prologue"));CHECK(has(m,"sce_start"));CHECK(!has(m,"qload"));
    CHECK(m.activate("prologue").type==Type::None);CHECK(m.confirming());shot("confirm-new-game");
    CHECK(m.activate("extra").type==Type::None);CHECK(m.page()==Page::Title);
    CHECK(m.answer_confirmation(false).type==Type::None);CHECK(!m.confirming());
    auto start=click(m,"prologue");CHECK(start.type==Type::NewGame);CHECK(start.asset=="DC4_SEN20181213B");
    m.pointer_down(1100,220);CHECK(m.pointer_up(1100,280).type==Type::None);
    m.pointer_down(1100,220);m.cancel_pointer();CHECK(m.pointer_up(1100,220).type==Type::None);
    shot("title");
    click(m,"config");CHECK(m.page()==Page::Config);
    for(int i=0;i<7;++i){m.set_config_page(i);CHECK(m.config_page()==i);CHECK(m.controls().size()>=8);CHECK(m.error().empty());shot("config"+std::to_string(i+1));}
    m.set_config_page(2);CHECK(has(m,"music_off"));CHECK(click(m,"music_off").type==Type::SettingsChanged);CHECK(!m.config().music_enabled);
    CHECK(click(m,"music_on").type==Type::SettingsChanged);CHECK(m.config().music_enabled);
    // Native BGM slider at y=278; test all slider banks by reading state after
    // a captured drag, including release outside the original hit rectangle.
    m.pointer_down(160,296);m.pointer_move(254,296);m.pointer_up(400,296);CHECK(m.config().music_volume>=90);
    m.set_config_page(1);m.activate("mspeed--");CHECK(m.config().text_speed==7);
    CHECK(m.activate("defaultparts").type==Type::None);CHECK(m.confirming());shot("confirm-reset");
    CHECK(m.config().text_speed==7);CHECK(m.back().type==Type::None);CHECK(m.config().text_speed==7);
    m.activate("defaultparts");CHECK(m.answer_confirmation(true).type==Type::SettingsChanged);CHECK(m.config().text_speed==8);
    m.open(Page::Extras);CHECK(has(m,"cgmode"));CHECK(has(m,"scemode"));CHECK(!has(m,"musmode"));
    m.set_variable(100,109,1);CHECK(has(m,"musmode"));shot("extras");
    click(m,"cgmode");CHECK(m.page()==Page::Gallery);CHECK(!has(m,"cg0"));shot("gallery-locked");
    m.mark_cg_seen("bg/dc4_ev_asa07b");m.mark_cg_seen("bg/dc4_ev_asa07a");m.open(Page::Gallery);CHECK(has(m,"cg0"));shot("gallery-unlocked");
    click(m,"cg0");shot("gallery-view");m.scroll(1);shot("gallery-variant");m.back();CHECK(has(m,"cg0"));
    for(const char* route:{"nin","sor","hyr","sii","miu","tiy","cmn","asa"}){m.activate(route);CHECK(m.page()==Page::Gallery);CHECK(m.error().empty());m.scroll(1);CHECK(m.error().empty());}
    m.open(Page::Scenes);CHECK(!has(m,"sce0"));shot("scenes-locked");
    m.mark_scene_seen("dc4_mid20190301a1");m.open(Page::Scenes);CHECK(has(m,"sce0"));shot("scenes-unlocked");
    const int selector=m.variable(99,100);
    CHECK(m.activate("sce0").type==Type::None);CHECK(m.confirming());shot("confirm-scene");
    m.scroll(1);CHECK(m.variable(3,6)==0);CHECK(m.variable(99,100)==selector);
    CHECK(m.answer_confirmation(false).type==Type::None);CHECK(has(m,"sce0"));
    auto scene=click(m,"sce0");CHECK(scene.type==Type::StartScene);CHECK(scene.asset=="DC4_MID20190301A1");
    m.open(Page::Music);CHECK(!has(m,"#music:0"));shot("music-locked");
    m.mark_music_seen("music/000");m.mark_music_seen("music/001");m.open(Page::Music);CHECK(has(m,"#music:0"));
    auto music=click(m,"#music:0");CHECK(music.type==Type::PlayMusic);CHECK(music.index==0);CHECK(music.asset=="music\\000");
    CHECK(m.activate("next").index==1);CHECK(m.activate("stop").type==Type::StopMusic);shot("music-unlocked");
    m.scroll(36);CHECK(m.error().empty());shot("music-last-page");
    m.activate("selmovie");shot("movies");CHECK(m.activate("movie01").type==Type::None);CHECK(m.confirming());shot("confirm-movie");CHECK(m.answer_confirmation(true).asset=="DC4_OP1.MPG");
}
}
int main(int argc,char** argv){profile();const char* installed=argc>1?argv[1]:std::getenv("DC4_TEST_DATA");if(installed){assets(installed);scene_selections(installed);}else std::cout<<"Installed menu asset checks skipped (set DC4_TEST_DATA).\n";return failures?1:0;}
