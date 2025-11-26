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
  uint32_t log_master_next_table_id;
  uint64_t sw_owner_icm_root_1;
  uint64_t sw_owner_icm_root_0;
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
#define MLX5_DBG_GET_FT_INFO  _IOWR('m', 17, struct mlx5_get_hca_cap)                                                                                                                                                             

#ifndef _KERNEL
#define MLX5_DBG_DEV_PATH _PATH_DEV"mlx5dbg"                                                                                                                                                                                     
#endif

#endif /* _MLX5DBGIO_H_ */
