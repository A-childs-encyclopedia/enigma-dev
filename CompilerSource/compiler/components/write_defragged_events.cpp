/**
  @file  write_defragged_events.cpp
  @brief Implements the method in the C++ plugin that writes an optimized event loop
         to the engine.
  
  @section License
    Copyright (C) 2008-2014 Josh Ventura
    This file is a part of the ENIGMA Development Environment.

    ENIGMA is free software: you can redistribute it and/or modify it under the
    terms of the GNU General Public License as published by the Free Software
    Foundation, version 3 of the license or any later version.

    This application and its source code is distributed AS-IS, WITHOUT ANY WARRANTY; 
    without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
    PURPOSE. See the GNU General Public License for more details.

    You should have recieved a copy of the GNU General Public License along
    with this code. If not, see <http://www.gnu.org/licenses/>
**/

#include "makedir.h"
#include <stdio.h>
#include <iostream>
#include <fstream>
#include <string>
#include <map>

#include "backend/ideprint.h"

using namespace std;


#include "syntax/syncheck.h"
#include "parser/parser.h"

#include "backend/EnigmaStruct.h" //LateralGM interface structures
#include "compiler/compile_common.h"

#include "compiler/event_reader/event_parser.h"
#include "languages/lang_CPP.h"

struct foundevent { int mid, id, count; foundevent(): mid(0),id(0),count(0) {} void f2(int m,int i) { id = i, mid = m; } void inc(int m,int i) { mid=m,id=i,count++; } void operator++(int) { count++; } };
map<string,foundevent> used_events;
typedef map<string,foundevent>::iterator evfit;

int lang_CPP::compile_write_defragd_events(compile_context &ctex)
{
  ofstream wto((makedir +"Preprocessor_Environment_Editable/IDE_EDIT_evparent.h").c_str());
  wto << gen_license;

  /* Generate a new list of events used by the objects in
  ** this game. Only events on this list will be exported.
  ***********************************************************/
  used_events.clear();
  for (int i = 0; i < ctex.es->gmObjectCount; i++)
    for (int ii = 0; ii < ctex.es->gmObjects[i].mainEventCount; ii++)
      for (int iii = 0; iii < ctex.es->gmObjects[i].mainEvents[ii].eventCount; iii++)
      {
        const int mid = ctex.es->gmObjects[i].mainEvents[ii].id, id = ctex.es->gmObjects[i].mainEvents[ii].events[iii].id;
        if (event_is_instance(mid,id))
          used_events[event_stacked_get_root_name(mid)].inc(mid,id);
        else
          used_events[event_get_function_name(mid,id)].inc(mid,id);
      }

  /* ********************************************************* *\
  ** Some events are included in all objects, even if the user **
  ** hasn't specified code for them. Account for those here.   **
  \* ********************************************************* */
  for (size_t i=0; i<event_sequence.size(); i++)
  {
    const int mid = event_sequence[i].first, id = event_sequence[i].second;
    if (event_has_default_code(mid,id)) // We may not be using this event, but if it's persistent... (constant or defaulted: mandatory)
    {
      used_events[event_get_function_name(mid,id)].f2(mid,id); // ...Reserve it anyway.

      for (po_i it = ctex.parsed_objects.begin(); it != ctex.parsed_objects.end(); it++) // Then shell it out into the other objects.
      {
        bool exists = false;
        for (unsigned j = 0; j < it->second->events.size(); j++)
          if (it->second->events[j]->main_id == mid and it->second->events[j]->id == id)
            { exists = true; break; }
        if (!exists)
          it->second->events.push_back(new parsed_event(main_context, mid, id, it->second, ctex.global, ""));
      }
    }
  }

  /* Now we forge a top-level tier for object declaration
  ** that defines default behavior for any obect's unused events.
  *****************************************************************/
  wto << "namespace enigma" << endl << "{" << endl;
  wto << "  struct event_parent: " << system_get_uppermost_tier() << endl;
  wto << "  {" << endl;
            for (evfit it = used_events.begin(); it != used_events.end(); it++)
            {
              const bool e_is_inst = event_is_instance(it->second.mid, it->second.id);
              wto << (e_is_inst ? "    virtual void    myevent_" : "    virtual variant myevent_") << it->first << "()";
              if (event_has_default_code(it->second.mid,it->second.id))
                wto << endl << "    {" << endl << "  " << event_get_default_code(it->second.mid,it->second.id) << endl << (e_is_inst ? "    }" : "    return 0;\n    }") << endl;
              else wto << (e_is_inst ? " { } // No default " : " { return 0; } // No default ") << event_get_human_name(it->second.mid,it->second.id) << " code." << endl;
            }
  wto << "    //virtual void unlink() {} // This is already declared at the super level." << endl;
  wto << "    virtual variant myevents_perf(int type, int numb) {return 0;}" << endl;
  wto << "    event_parent() {}" << endl;
  wto << "    event_parent(unsigned _x, int _y): " << system_get_uppermost_tier() << "(_x,_y) {}" << endl;
  wto << "  };" << endl;
  wto << "}" << endl;
  wto.close();


  /* Now we write the actual event sequence code, as
  ** well as an initializer function for the whole system.
  ***************************************************************/
  wto.open((makedir +"Preprocessor_Environment_Editable/IDE_EDIT_events.h").c_str());
  wto << gen_license;
  wto << "namespace enigma" << endl << "{" << endl;

  // Start by defining storage locations for our event lists to iterate.
  for (evfit it = used_events.begin(); it != used_events.end(); it++)
    wto  << "  event_iter *event_" << it->first << "; // Defined in " << it->second.count << " objects" << endl;

  // Here's the initializer
  wto << "  int event_system_initialize()" << endl << "  {" << endl;
    wto  << "    events = new event_iter[" << used_events.size() << "]; // Allocated here; not really meant to change." << endl;
    int obj_high_id = ctex.parsed_objects.rbegin() != ctex.parsed_objects.rend() ? ctex.parsed_objects.rbegin()->first : 0;
    wto  << "    objects = new objectid_base[" << (obj_high_id+1) << "]; // Allocated here; not really meant to change." << endl;

    int ind = 0;
    for (evfit it = used_events.begin(); it != used_events.end(); it++)
      wto  << "    event_" << it->first << " = events + " << ind++ << ";  event_" << it->first << "->name = \"" << event_get_human_name(it->second.mid,it->second.id) << "\";" << endl;
    wto << "    return 0;" << endl;
  wto << "  }" << endl;

    // Game setting initaliser
  wto << "  int game_settings_initialize()" << endl << "  {" << endl;
/*    wto  << "    window_set_fullscreen(" << ctex.es->gameSettings.startFullscreen << ");" << endl;
    // This should only affect texture interpolation if it has not already been enabled
    wto  << "    texture_set_interpolation(" << ctex.es->gameSettings.interpolate << "); " << endl;
    if (!ctex.es->gameSettings.displayCursor)
        wto  << "    window_set_cursor(cr_none);" << endl;
    wto  << "    window_set_region_scale(" << ctex.es->gameSettings.scaling/100.0 << ", 0);" << endl;
    wto  << "    window_set_sizeable(" << ctex.es->gameSettings.allowWindowResize << ");" << endl;
    if (ctex.es->gameSettings.alwaysOnTop)
        wto  << "    window_set_stayontop(true);" << endl;
    wto  << "    window_set_showborder(" << !ctex.es->gameSettings.dontDrawBorder << ");" << endl;
    wto  << "    window_set_showicons(" << !ctex.es->gameSettings.dontShowButtons << ");" << endl;*/  //TODO: LGM needs settings sorted before reenabling

    wto << "    return 0;" << endl;
  wto << "  }" << endl;

  wto << "  variant ev_perf(int type, int numb)\n  {\n    return ((enigma::event_parent*)(instance_event_iterator->inst))->myevents_perf(type, numb);\n  }\n";

  /* Some Super Checks are more complicated than others, requiring a function. Export those functions here. */
  for (evfit it = used_events.begin(); it != used_events.end(); it++)
    wto << event_get_super_check_function(it->second.mid, it->second.id);

  /* Now the event sequence */
  bool using_gui = false;
  wto << "  int ENIGMA_events()" << endl << "  {" << endl;
    for (size_t i=0; i<event_sequence.size(); i++)
    {
      // First, make sure we're actually using this event in some object
      const int mid = event_sequence[i].first, id = event_sequence[i].second;
      evfit it = used_events.find(event_is_instance(mid,id) ? event_stacked_get_root_name(mid) : event_get_function_name(mid,id));
      if (it == used_events.end()) continue;
      if (mid == 7 && (id >= 10 && id <= 25)) continue;   //User events, don't want to be run in the event sequence. TODO: Remove hard-coded values.
      if (mid == 8 && id == 64)
      {
          string seqcode = event_forge_sequence_code(mid,id,it->first);
          if (seqcode != "")
            using_gui = true;

          continue;       // Don't want gui loop to be added
      }

      string seqcode = event_forge_sequence_code(mid,id,it->first);
      if (seqcode != "")
        wto << seqcode,
        wto << "    " << endl,
        wto << "    enigma::update_globals();" << endl,
        wto << "    " << endl;
    }
    wto << "    after_events:" << endl;
    if (ctex.es->gameSettings.letEscEndGame)
        wto << "    if (keyboard_check_pressed(vk_escape)) game_end();" << endl;
    if (ctex.es->gameSettings.letF4SwitchFullscreen)
        wto << "    if (keyboard_check_pressed(vk_f4)) window_set_fullscreen(!window_get_fullscreen());" << endl;
    if (ctex.es->gameSettings.letF1ShowGameInfo)
        wto << "    if (keyboard_check_pressed(vk_f1)) show_info();" << endl;
    if (ctex.es->gameSettings.letF9Screenshot)
        wto << "    if (keyboard_check_pressed(vk_f9)) {}" << endl;   //TODO: Screenshot function
    if (ctex.es->gameSettings.letF5SaveF6Load)  //TODO: uncomment after game save and load fucntions implemented
    {
        wto << "    //if (keyboard_check_pressed(vk_f5)) game_save('_save" << ctex.es->gameSettings.gameId << ".sav');" << endl;
        wto << "    //if (keyboard_check_pressed(vk_f6)) game_load('_save" << ctex.es->gameSettings.gameId << ".sav');" << endl;
    }
    // Handle room switching/game restart.
    wto << "    enigma::dispose_destroyed_instances();" << endl;
    wto << "    enigma::rooms_switch();" << endl;
    wto << "    enigma::set_room_speed(room_speed);" << endl;
    wto << "    " << endl;
    wto << "    return 0;" << endl;
  wto << "  } // event function" << endl;

  wto << "  bool gui_used = " << using_gui << ";" << endl;
  // Done, end the namespace
  wto << "} // namespace enigma" << endl;
  wto.close();

  return 0;
}

bool event_used_by_something(string name)
{
  return used_events.find(name) != used_events.end();
}
