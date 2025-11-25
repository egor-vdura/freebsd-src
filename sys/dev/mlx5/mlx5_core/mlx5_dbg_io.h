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

#define MLX5_DBG_GET_EQ_LIST  _IOWR('m', 7, struct mlx5_get_eq_list)                                                                                                                                                             
#define MLX5_DBG_GET_EQ_INFO  _IOWR('m', 8, struct mlx5_get_eq_info)                                                                                                                                                             
#define MLX5_DBG_GET_CQ_LIST  _IOWR('m', 9, struct mlx5_get_cq_list)                                                                                                                                                             
#define MLX5_DBG_GET_CQ_INFO  _IOWR('m', 10, struct mlx5_get_cq_info)                                                                                                                                                            
#define MLX5_DBG_GET_TIR_LIST  _IOWR('m', 11, struct mlx5_get_cq_list)                                                                                                                                                             
#define MLX5_DBG_GET_TIR_INFO  _IOWR('m', 12, struct mlx5_get_tir_info)                                                                                                                                                             

#ifndef _KERNEL
#define MLX5_DBG_DEV_PATH _PATH_DEV"mlx5dbg"                                                                                                                                                                                     
#endif

#endif /* _MLX5DBGIO_H_ */
