#include "dc4/confirm_dialog.h"
#include "dc4/save_screen.h"
#include <iostream>

namespace {
int failures=0;
#define CHECK(x) do{if(!(x)){std::cerr<<__LINE__<<": " #x "\n";++failures;}}while(false)
using namespace dc4ui;
void dialog(){
    ConfirmDialog d;
    for(int i=0;i<int(ConfirmKind::Count);++i){
        auto kind=ConfirmKind(i);CHECK(d.open(kind));
        // Press outside, drag onto Yes: no action. A cancelled gesture must
        // not arm the next dialog, either.
        d.pointer_down(10,10);CHECK(d.pointer_up(540,380)==ConfirmResult::None);
        d.pointer_down(540,380);d.cancel_pointer();CHECK(d.pointer_up(540,380)==ConfirmResult::None);
        d.pointer_down(540,380);d.close();CHECK(d.open(kind));
        CHECK(d.pointer_up(540,380)==ConfirmResult::None);
        d.pointer_down(600,445);CHECK(d.pointer_up(600,445)==ConfirmResult::None);CHECK(d.dont_confirm());
        CHECK(d.answer(false)==ConfirmResult::No);
        CHECK(d.open(kind));CHECK(!d.dont_confirm());
        d.set_dont_confirm(true);d.pointer_down(540,380);CHECK(d.pointer_up(540,380)==ConfirmResult::Yes);
        CHECK(!d.active());CHECK(d.answer(true)==ConfirmResult::None);
        CHECK(!d.open(kind)); // Suppression applies only after accepting Yes.
    }
    ConfirmDialog fresh;CHECK(fresh.open(ConfirmKind::Load));
    CHECK(!fresh.hit_no(699,421)); // No invisible legacy hit rectangles.
    fresh.pointer_down(720,380);CHECK(fresh.pointer_up(720,380)==ConfirmResult::No);
}
void saves(){
    SaveScreen s;s.set_confirm_enabled(true);s.set_navigation(true,true,true);
    s.set_slot_occupied(0,true);
    for(auto mode:{SaveMode::Save,SaveMode::Load}){
        s.set_mode(mode);
        CHECK(s.activate({SaveActionType::Slot,0}).type==SaveActionType::None);CHECK(s.confirming());
        CHECK(s.activate({SaveActionType::Page,2}).type==SaveActionType::None);CHECK(s.page()==0);
        CHECK(s.answer_confirmation(false).type==SaveActionType::None);CHECK(!s.confirming());
        CHECK(s.activate({SaveActionType::Slot,0}).type==SaveActionType::None);
        s.pointer_down(540,380);auto accepted=s.pointer_up(540,380);
        CHECK(accepted.type==SaveActionType::Slot&&accepted.index==0);CHECK(!s.confirming());
        CHECK(s.pointer_up(540,380).type==SaveActionType::None);
    }
    s.set_mode(SaveMode::Save);CHECK(s.activate({SaveActionType::Slot,1}).type==SaveActionType::None);
    CHECK(s.confirming());CHECK(s.answer_confirmation(true).index==1);
    s.set_mode(SaveMode::Load);CHECK(s.activate({SaveActionType::Slot,1}).type==SaveActionType::None);CHECK(!s.confirming());
    s.activate({SaveActionType::Slot,0});s.set_slot_occupied(0,false);
    CHECK(s.answer_confirmation(true).type==SaveActionType::None);
    s.set_shortcuts(true,true,true);
    for(auto type:{SaveActionType::LatestLoad,SaveActionType::AutoLoad,SaveActionType::QuickLoad,SaveActionType::Title,SaveActionType::Quit}){
        CHECK(s.activate({type}).type==SaveActionType::None);CHECK(s.confirming());
        CHECK(s.answer_confirmation(true).type==type);CHECK(!s.confirming());
    }
    s.activate({SaveActionType::QuickLoad});s.set_mode(SaveMode::Save);
    CHECK(!s.confirming());CHECK(s.answer_confirmation(true).type==SaveActionType::None);
}
}
int main(){dialog();saves();return failures?1:0;}
