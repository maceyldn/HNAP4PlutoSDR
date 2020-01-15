/*
 * mac_ue.c
 *
 *  Created on: Dec 10, 2019
 *      Author: lukas
 */

#include "mac_ue.h"

// Allocate memory for MAC instance and init it
MacUE mac_ue_init()
{
	MacUE mac = calloc(sizeof(struct MacUE_s), 1);
	mac->msg_control_queue = ringbuf_create(MAC_MSG_BUF_SIZE);
	mac->fragmenter = mac_frag_init();
	mac->reassembler = mac_assmbl_init();
	return mac;
}

// Mac layer needs a pointer to phy layer in order to call
// interface functions
void mac_ue_set_phy_interface(MacUE mac, struct PhyUE_s* phy)
{
	mac->phy = phy;
}

// Generic handler for received messages
int mac_ue_handle_message(MacUE mac, MacMessage msg)
{
	MacDataFrame frame;

	switch (msg->type) {
	case associate_response:
		// Assert correct version
		if (msg->hdr.AssociateResponse.protoVersion != PROTO_VERSION) {
			LOG(ERR,"[MAC UE] Wrong Protocol version! got %d expected %d",
					msg->hdr.AssociateResponse.protoVersion,PROTO_VERSION);
		}
		// Check whether we were successfully added
		if (msg->hdr.AssociateResponse.response == assoc_resp_success) {
			mac->is_associated = 1;
			LOG(INFO,"[MAC UE] successfully associated!\n");
		} else {
			LOG(INFO,"[MAC UE] NACK for assoc req: response is %d\n",msg->hdr.AssociateResponse.response);
		}
		break;
	case dl_mcs_info:
		mac->dl_mcs = msg->hdr.DLMCSInfo.mcs;
		phy_ue_set_mcs_dl(mac->phy, mac->dl_mcs);
		LOG(INFO,"[MAC UE] switching to DL MCS %d\n",mac->dl_mcs);

		// TODO generate answer
		break;
	case ul_mcs_info:
		mac->ul_mcs = msg->hdr.ULMCSInfo.mcs;
		LOG(INFO,"[MAC UE] switching to UL MCS %d\n",mac->ul_mcs);
		// TODO generate answer
		break;
	case timing_advance:
		// TODO implement timing advance
		break;
	case dl_data:
		frame = mac_assmbl_reassemble(mac->reassembler, msg);
		if (frame != NULL) {
			printf("[MAC UE] rec %d bytes: %s\n",frame->size,frame->data);
			//TODO forward received frame to higher layer
			dataframe_destroy(frame);
		}
		break;
	default:
		LOG(WARN,"[MAC UE] unexpected MacMsg ID: %d\n",msg->type);
		return 0;
	}
	mac_msg_destroy(msg);
	return 1;
}

// UE scheduler. Is called once per subframe
// Will check the ctrl message and data message queues and try
// to map it to slots. Before running the scheduler, ensure that
// the slot assignment variables are up to date
void mac_ue_run_scheduler(MacUE mac)
{
	uint queuesize = mac_frag_get_buffersize(mac->fragmenter);
	uint slotsize = get_tbs_size(mac->phy->common,mac->ul_mcs)/8;
	int num_assigned = num_slot_assigned(mac->ul_data_assignments,MAC_ULDATA_SLOTS,mac->userid);

	// ensure association
	if (mac->is_associated == 0) {
		return;
	}
	// check data queue
	if (queuesize > 0) {
		// check if an UL data slot is available
		if (num_assigned>0) {
			// TODO check if we can transmit all our data or if we have
			// to request more slots
			for (int i=0; i<MAC_ULDATA_SLOTS; i++) {
				if (mac->ul_data_assignments[i] == mac->userid && queuesize>0) {
					LogicalChannel chan = lchan_create(slotsize, CRC16);
					lchan_add_all_msgs(chan, mac->msg_control_queue);
					MacMessage msg = mac_frag_get_fragment(mac->fragmenter,
													chan->payload_len-chan->writepos,1);
					lchan_add_message(chan, msg);
					mac_msg_destroy(msg);
					lchan_calc_crc(chan);
					phy_map_ulslot(mac->phy,chan,i, mac->ul_mcs);
					lchan_destroy(chan);
					queuesize = mac_frag_get_buffersize(mac->fragmenter);
				}
			}
		} else {
			// We have data but no UL data slot. add request
			MacMessage msg = mac_msg_create_ul_req(queuesize);
			ringbuf_put(mac->msg_control_queue, msg);
		}
	}
	// UL ctrl slot available?
	if (num_slot_assigned(mac->ul_ctrl_assignments, MAC_ULCTRL_SLOTS, mac->userid)>0) {
		// if there are no ctrl messages to be sent we have to create keepalive
		if (ringbuf_isempty(mac->msg_control_queue)) {
			MacMessage msg = mac_msg_create_keepalive();
			ringbuf_put(mac->msg_control_queue, msg);
		}

		// create logical channel with control messages
		LogicalChannel chan = lchan_create(get_ulctrl_slot_size(mac->phy->common)/8,CRC8);
		lchan_add_all_msgs(chan, mac->msg_control_queue);
		lchan_calc_crc(chan);
		// find the ulctrl slot in which we can transmit
		for (int i=0; i<MAC_ULCTRL_SLOTS; i++) {
			if (mac->ul_ctrl_assignments[i] == mac->userid) {
				phy_map_ulctrl(mac->phy,chan,i);
				break;
			}
		}
		lchan_destroy(chan);
	}
}

// Main interface function that is called from PHY when receiving a
// logical channel. Function will extract messages and call
// the message handler
void mac_ue_rx_channel(MacUE mac, LogicalChannel chan)
{
	// Verify the CRC
	if(!lchan_verify_crc(chan)) {
		LOG(INFO, "[MAC UE] lchan CRC invalid. Dropping.\n");
		return;
	}

	MacMessage msg = NULL;
	// Get all messages from the logical channel and handle them
	do {
		lchan_parse_next_msg(chan, 0);
		if (msg) {
			mac_ue_handle_message(mac,msg);
		}
	} while (msg != NULL);

	lchan_destroy(chan);
}

// Add a higher layer packet to the tx queue
int mac_ue_add_txdata(MacUE mac, MacDataFrame frame)
{
	return mac_frag_add_frame(mac->fragmenter, frame);
}
