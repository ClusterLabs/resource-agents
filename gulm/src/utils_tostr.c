/******************************************************************************
*******************************************************************************
**
**  Copyright (C) Sistina Software, Inc.  2002-2003  All rights reserved.
**  Copyright (C) 2004 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/
#include "gio_wiretypes.h"

char *gio_Err_to_str(int x)
{
   char *t="Unknown GULM Err";
   switch(x) {
      case gio_Err_Ok:              t = "Ok"; break;

      case gio_Err_BadLogin:        t = "Bad Login"; break;
      case gio_Err_BadCluster:      t = "Bad Cluster ID"; break;
      case gio_Err_BadConfig:       t = "Incompatible configurations"; break;
      case gio_Err_BadGeneration:   t = "Bad Generation ID"; break;
      case gio_Err_BadWireProto:    t = "Bad Wire Protocol Version"; break;

      case gio_Err_NotAllowed:      t = "Not Allowed"; break;
      case gio_Err_Unknown_Cs:      t = "Uknown Client"; break;
      case gio_Err_BadStateChg:     t = "Bad State Change"; break;
      case gio_Err_MemoryIssues:    t = "Memory Problems"; break;

      case gio_Err_TryFailed:       t = "Try Failed"; break;
      case gio_Err_AlreadyPend:     t = "Request Already Pending"; break;
      case gio_Err_Canceled:        t = "Request Canceled"; break;
   }
   return t;
}

char *gio_mbrupdate_to_str(int x)
{
   char *t="Unknown Membership Update";
   switch(x){
      case gio_Mbr_Logged_in:  t = "Logged in";  break;
      case gio_Mbr_Logged_out: t = "Logged out"; break;
      case gio_Mbr_Expired:    t = "Expired";    break;
      case gio_Mbr_Killed:     t = "Fenced";     break;
      case gio_Mbr_OM_lgin:    t = "Was Logged in"; break;
   }
   return t;
}

char *gio_I_am_to_str(int x)
{
   switch(x){
      case gio_Mbr_ama_Slave:   return "Slave"; break;
      case gio_Mbr_ama_Pending: return "Pending"; break;
      case gio_Mbr_ama_Arbitrating: return "Arbitrating"; break;
      case gio_Mbr_ama_Master:  return "Master"; break;
      case gio_Mbr_ama_Resource:  return "Service"; break;
      case gio_Mbr_ama_Client:  return "Client"; break;
      default: return "Unknown I_am state"; break;
   }
}

char *gio_license_states(int x)
{
   switch(x) {
      case 0:  return "valid";   break;
      case 1:  return "expired"; break;
      case 2:  return "invalid"; break;
      default: return "unknown"; break;
   }
}


char *gio_opcodes(int x)
{
   switch(x) {
#define CP(x) case (x): return #x ; break
      CP(gulm_err_reply);

      CP(gulm_core_login_req);
      CP(gulm_core_login_rpl);
      CP(gulm_core_logout_req);
      CP(gulm_core_logout_rpl);
      CP(gulm_core_reslgn_req);
      CP(gulm_core_beat_req);
      CP(gulm_core_beat_rpl);
      CP(gulm_core_mbr_req);
      CP(gulm_core_mbr_updt);
      CP(gulm_core_mbr_lstreq);
      CP(gulm_core_mbr_lstrpl);
      CP(gulm_core_mbr_force);
      CP(gulm_core_res_req);
      CP(gulm_core_res_list);
      CP(gulm_core_state_req);
      CP(gulm_core_state_chgs);
      CP(gulm_core_shutdown);
      CP(gulm_core_forcepend);

      CP(gulm_info_stats_req);
      CP(gulm_info_stats_rpl);
      CP(gulm_info_set_verbosity);
      CP(gulm_socket_close);
      CP(gulm_info_slave_list_req);
      CP(gulm_info_slave_list_rpl);

      CP(gulm_lock_login_req);
      CP(gulm_lock_login_rpl);
      CP(gulm_lock_logout_req);
      CP(gulm_lock_logout_rpl);
      CP(gulm_lock_state_req);
      CP(gulm_lock_state_rpl);
      CP(gulm_lock_state_updt);
      CP(gulm_lock_action_req);
      CP(gulm_lock_action_rpl);
      CP(gulm_lock_action_updt);
      CP(gulm_lock_update_rpl);
      CP(gulm_lock_cb_state);
      CP(gulm_lock_cb_dropall);
      CP(gulm_lock_drop_exp);
      CP(gulm_lock_dump_req);
      CP(gulm_lock_dump_rpl);
      CP(gulm_lock_rerunqueues);

#undef CP
      default: return "Unknown Op Code"; break;
   }
}
/* vim: set ai cin et sw=3 ts=3 : */
