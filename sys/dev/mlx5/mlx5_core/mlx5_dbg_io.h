/*-
 * Copyright (c) 2018, Mellanox Technologies, Ltd.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY AUTHOR AND CONTRIBUTORS `AS IS' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#ifndef _MLX5DBGIO_H_
#define _MLX5DBGIO_H_

#if defined(__FreeBSD__)
#include <sys/ioccom.h>
#elif defined(__linux__)
#include <linux/ioctl.h>
#else
#error "Unknown platform"
#endif

struct mlx5_tool_addr {
  uint32_t domain;
  uint8_t bus;
  uint8_t slot;
  uint8_t func;
};

struct mlx5_get_eq_list {
  struct mlx5_tool_addr devaddr;
  uint32_t *eq_list;
  size_t eq_list_len;
};

struct mlx5_get_eq_info {
  struct mlx5_tool_addr devaddr;
  uint8_t eqn; /* in */
  uint32_t num_eqes;
  uint32_t intr;
  uint32_t log_pg_sz;
  uint32_t status;
  uint32_t ec;
  uint32_t oi;
  uint32_t st;
  uint32_t page_offset;
  uint32_t uar_page;
  uint32_t consumer_counter;
  uint32_t producer_counter;
  uint32_t syndrome;
  uint64_t event_bitmask;
  uint32_t status2;
};

struct mlx5_get_cq_list {
  struct mlx5_tool_addr devaddr;
  uint32_t *cq_list;
  size_t cq_list_len;
};

struct mlx5_get_cq_info {
  struct mlx5_tool_addr devaddr;
  uint32_t cqn; /* in */
  uint32_t status;
  uint32_t dbr_umem_valid;
  uint32_t cqe_sz;
  uint32_t cc;
  uint32_t scqe_break_moderation_en;
  uint32_t oi;
  uint32_t cq_period_mode;
  uint32_t cqe_compression_en;
  uint32_t mini_cqe_res_format;
  uint32_t st;
  uint32_t page_offset;
  uint32_t log_cq_size;
  uint32_t uar_page;
  uint32_t cq_period;
  uint32_t cq_max_count;
  uint32_t c_eqn;
  uint32_t log_page_size;
  uint32_t last_notified_index;
  uint32_t consumer_counter;
  uint32_t producer_counter;
  uint64_t dbr_addr;
};

struct mlx5_get_tir_list {
  struct mlx5_tool_addr devaddr;
  uint32_t *tir_list;
  size_t tir_list_len;
};

struct mlx5_get_tir_info {
  struct mlx5_tool_addr devaddr;
  uint32_t tirn; /* in */
  uint32_t disp_type;
  uint32_t tls_en;
  uint32_t lro_timeout_period_usecs;
  uint32_t lro_enable_mask;
  uint32_t lro_max_msg_sz;
  uint32_t inline_rqn;
  uint32_t rx_hash_symmetric;
  uint32_t tunneled_offload_en;
  uint32_t indirect_table;
  uint32_t rx_hash_fn;
  uint32_t self_lb_en;
  uint32_t transport_domain;
};

struct mlx5_get_rqt_list {
  struct mlx5_tool_addr devaddr;
  uint32_t *rqt_list;
  size_t rqt_list_len;
};

struct mlx5_get_rqt_info {
  struct mlx5_tool_addr devaddr;
  uint32_t rqtn; /* in */
  uint32_t rqt_max_size;
  uint32_t rqt_actual_size;
  uint32_t rq_num[256];
};

struct mlx5_get_qp_info {
  struct mlx5_tool_addr devaddr;
  uint32_t qpn; /* in */
  uint32_t state;
  uint32_t lag_tx_port_affinity;
  uint32_t st;
  uint32_t isolate_vl_tc;
  uint32_t pm_state;
  uint32_t req_e2e_credit_mode;
  uint32_t offload_type;
  uint32_t end_padding_mode;
  uint32_t wq_signature;
  uint32_t block_lb_mc;
  uint32_t atomic_like_write_en;
  uint32_t latency_sensitive;
  uint32_t drain_sigerr;
  uint32_t pd;
  uint32_t mtu;
  uint32_t log_msg_max;
  uint32_t log_rq_size;
  uint32_t log_rq_stride;
  uint32_t no_sq;
  uint32_t log_sq_size;
  uint32_t retry_mode;
  uint32_t ts_format;
  uint32_t rlky;
  uint32_t ulp_stateless_offload_mode;
  uint32_t counter_set_id;
  uint32_t uar_page;
  uint32_t user_index;
  uint32_t log_page_size;
  uint32_t remote_qpn;
  uint32_t log_ack_req_freq;
  uint32_t log_sra_max;
  uint32_t retry_count;
  uint32_t rnr_retry;
  uint32_t fre;
  uint32_t cur_rnr_retry;
  uint32_t cur_retry_count;
  uint32_t next_send_psn;
  uint32_t log_num_dci_stream_channels;
  uint32_t cqn_snd;
  uint32_t log_num_dci_errored_streams;
  uint32_t deth_sqpn;
  uint32_t last_acked_psn;
  uint32_t ssn;
  uint32_t log_rra_max;
  uint32_t atomic_mode;
  uint32_t rre;
  uint32_t rwe;
  uint32_t rae;
  uint32_t page_offset;
  uint32_t cd_slave_receive;
  uint32_t cd_slave_send;
  uint32_t cd_master;
  uint32_t min_rnr_nak;
  uint32_t next_rcv_psn;
  uint32_t xrcd;
  uint32_t cqn_rcv;
  uint64_t dbr_addr;
  uint32_t q_key;
  uint32_t rq_type;
  uint32_t srqn_rmpn_xrqn;
  uint32_t rmsn;
  uint32_t hw_sq_wqebb_counter;
  uint32_t sw_sq_wqebb_counter;
  uint32_t hw_rq_counter;
  uint32_t sw_rq_counter;
  uint32_t cgs;
  uint32_t cs_req;
  uint32_t cs_res;
  uint64_t dc_access_key;
  uint32_t dbr_umem_valid;
};

struct mlx5_get_hca_cap {
  struct mlx5_tool_addr devaddr;
  uint32_t nic_flow_table;
};

typedef enum mlx5_fg_match_criteria_e {
  MLX5_MATCH_CRITERIA_OUTER_HEADERS = 0x01,
  MLX5_MATCH_CRITERIA_MISC_PARAMS   = 0x02,
  MLX5_MATCH_CRITERIA_INNER_HEADERS = 0x04,
} mlx5_fg_match_criteria_t;

struct match_params {
  /* Outer headers */
  uint32_t smac_47_16;
  uint32_t smac_15_0;
  uint32_t ethertype;
  uint32_t dmac_47_16;
  uint32_t dmac_15_0;
  uint32_t first_prio;
  uint32_t first_cfi;
  uint32_t first_vid;
  uint32_t ip_protocol;
  uint32_t ip_dscp;
  uint32_t ip_ecn;
  uint32_t cvlan_tag;
  uint32_t svlan_tag;
  uint32_t frag;
  uint32_t ip_version;
  uint32_t tcp_flags;
  uint32_t tcp_sport;
  uint32_t tcp_dport;
  uint32_t udp_sport;
  uint32_t udp_dport;
  uint8_t src_ipv4_src_ipv6[16];
  uint8_t dst_ipv4_dst_ipv6[16];

  /* Misc parameters */
  uint32_t source_sqn;
  uint32_t source_port;
  uint32_t outer_second_prio;
  uint32_t outer_second_cfi;
  uint32_t outer_second_vid;
  uint32_t inner_second_prio;
  uint32_t inner_second_cfi;
  uint32_t inner_second_vid;
  uint32_t outer_second_vlan_tag;
  uint32_t inner_second_vlan_tag;
  uint32_t gre_protocol;
  uint32_t gre_key_h;
  uint32_t gre_key_l;
  uint32_t vxlan_vni;
  uint32_t geneve_vni;
  uint32_t geneve_oam;
  uint32_t outer_ipv6_flow_label;
  uint32_t inner_ipv6_flow_label;
  uint32_t geneve_opt_len;
  uint32_t geneve_protocol_type;
  uint32_t bth_dst_qp;
};

#define MAX_DESTINATIONS_NR 16
#define MAX_FLOW_COUNTER_NR 16

struct mlx5_fte_dest {
  uint32_t destination_type;
  uint32_t destination_id;
  uint32_t destination_table_type;
};

struct mlx5_flow_counter {
  uint32_t flow_counter_id;
};

struct mlx5_get_fte_info {
  struct mlx5_tool_addr devaddr;
  uint32_t ft_id; /* in */
  uint32_t flow_index; /* in */
  uint32_t match_type; /* in */

  uint32_t group_id;
  uint32_t flow_tag;
  uint32_t action;
  uint32_t destination_list_size;
  uint32_t flow_counter_list_size;
  uint32_t packet_reformat_id;
  uint32_t modify_header_id;
  struct match_params match_value;
  struct mlx5_fte_dest destinations[MAX_DESTINATIONS_NR];
  struct mlx5_flow_counter flow_counters[MAX_FLOW_COUNTER_NR];
};

struct mlx5_fg_info {
  uint32_t group_id;
  uint32_t start_flow_index;
  uint32_t end_flow_index;
  uint32_t match_criteria_enable;
  struct match_params mp;
};

#define MLX5_MAX_FG_CNT 4

struct mlx5_get_ft_info {
  struct mlx5_tool_addr devaddr;
  uint32_t ft_id; /* in */
  uint32_t reformat_en;
  uint32_t decap_en;
  uint32_t sw_owner;
  uint32_t termination_table;
  uint32_t table_miss_action;
  uint32_t level;
  uint32_t log_size;
  uint32_t table_miss_id;
  uint32_t lag_master_next_table_id;
  uint64_t sw_owner_icm_root_1;
  uint64_t sw_owner_icm_root_0;
  uint32_t flow_groups_cnt;
  struct mlx5_fg_info flow_groups[MLX5_MAX_FG_CNT];
};

struct mlx5_get_sq_info {
  struct mlx5_tool_addr devaddr;
  uint32_t sqn; /* in */
  uint32_t rlkey;
  uint32_t cd_master;
  uint32_t fre;
  uint32_t flush_in_error_en;
  uint32_t allow_multi_pkt_send_wqe;
  uint32_t min_wqe_inline_mode;
  uint32_t state;
  uint32_t reg_umr;
  uint32_t allow_swp;
  uint32_t ts_format;
  uint32_t user_index;
  uint32_t cqn;
  uint32_t packet_pacing_rate_limit_index;
  uint32_t tis_lst_sz;
  uint32_t qos_queue_group_id;
  uint32_t queue_handle;
  uint32_t tis_num_0;
};

struct mlx5_wq_info {
  uint32_t wq_type;
  uint32_t wq_signature;
  uint32_t end_padding_mode;
  uint32_t cd_slave;
  uint32_t hds_skip_first_sge;
  uint32_t log2_hds_buf_size;
  uint32_t page_offset;
  uint32_t lwm;
  uint32_t pd;
  uint32_t uar_page;
  uint32_t dbr_addr;
  uint32_t hw_counter;
  uint32_t sw_counter;
  uint32_t log_wq_stride;
  uint32_t log_wq_pg_sz;
  uint32_t log_wq_sz;
  uint32_t dbr_umem_valid;
  uint32_t wq_umem_valid;
  uint32_t single_wqe_log_num_of_strides;
  uint32_t two_byte_shift_en;
  uint32_t single_stride_log_num_of_bytes;
};

struct mlx5_get_rq_info {
  struct mlx5_tool_addr devaddr;
  uint32_t rqn; /* in */
  uint32_t rlkey;
  uint32_t delay_drop_en;
  uint32_t scatter_fcs;
  uint32_t vlan_strip_disable;
  uint32_t mem_rq_type;
  uint32_t state;
  uint32_t flush_in_error_en;
  uint32_t ts_format;
  uint32_t user_index;
  uint32_t cqn;
  uint32_t counter_set_id;
  uint32_t rmpn;
  struct mlx5_wq_info wq;
};

#define MLX5_DBG_GET_EQ_LIST  _IOWR('m', 7, struct mlx5_get_eq_list)
#define MLX5_DBG_GET_EQ_INFO  _IOWR('m', 8, struct mlx5_get_eq_info)
#define MLX5_DBG_GET_CQ_LIST  _IOWR('m', 9, struct mlx5_get_cq_list)
#define MLX5_DBG_GET_CQ_INFO  _IOWR('m', 10, struct mlx5_get_cq_info)
#define MLX5_DBG_GET_TIR_LIST  _IOWR('m', 11, struct mlx5_get_cq_list)
#define MLX5_DBG_GET_TIR_INFO  _IOWR('m', 12, struct mlx5_get_tir_info)
#define MLX5_DBG_GET_RQT_LIST  _IOWR('m', 13, struct mlx5_get_rqt_list)
#define MLX5_DBG_GET_RQT_INFO  _IOWR('m', 14, struct mlx5_get_rqt_info)
#define MLX5_DBG_GET_QP_INFO  _IOWR('m', 15, struct mlx5_get_qp_info)
#define MLX5_DBG_GET_HCA_CAP  _IOWR('m', 16, struct mlx5_get_hca_cap)
#define MLX5_DBG_GET_FT_INFO  _IOWR('m', 17, struct mlx5_get_ft_info)
#define MLX5_DBG_GET_FTE_INFO  _IOWR('m', 18, struct mlx5_get_fte_info)
#define MLX5_DBG_GET_SQ_INFO  _IOWR('m', 19, struct mlx5_get_sq_info)
#define MLX5_DBG_GET_RQ_INFO  _IOWR('m', 20, struct mlx5_get_rq_info)

#ifndef _KERNEL
#define MLX5_DBG_DEV_PATH _PATH_DEV"mlx5dbg"
#endif

#endif /* _MLX5DBGIO_H_ */
