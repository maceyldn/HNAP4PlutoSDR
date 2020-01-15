/*
 * mac_ue.h
 *
 *  Created on: Dec 10, 2019
 *      Author: lukas
 */

#ifndef MAC_MAC_UE_H_
#define MAC_MAC_UE_H_

#include "mac_channels.h"
#include "mac_common.h"
#include "mac_config.h"
#include "mac_fragmentation.h"
#include "../phy/phy_ue.h"

struct MacUE_s {
	uint8_t userid;

	uint8_t dl_mcs;
	uint8_t ul_mcs;
	uint timing_advance;

	ringbuf msg_control_queue;
	MacFrag fragmenter;
	MacAssmbl reassembler;

	uint8_t ul_ctrl_assignments[MAC_ULCTRL_SLOTS];
	uint8_t ul_data_assignments[MAC_DLDATA_SLOTS];
	uint8_t dl_data_assignments[MAC_ULDATA_SLOTS];

	uint8_t is_associated;

	struct PhyUE_s* phy;
};

typedef struct MacUE_s* MacUE;

/************ GENERAL MAC CONFIG FUNCTIONS **********************/
MacUE mac_ue_init();
void mac_ue_set_phy_interface(MacUE mac, PhyUE phy);

/************** MAC INTERFACE FUNCTIONS *************************/
void mac_ue_run_scheduler(MacUE mac);
void mac_ue_rec_channel(MacUE mac, LogicalChannel chan);
int  mac_ue_add_txdata(MacUE mac, MacDataFrame frame);

#endif /* MAC_MAC_UE_H_ */
