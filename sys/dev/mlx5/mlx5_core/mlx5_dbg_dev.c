/*
 * src/tools/ipoib_perf/mlx5_dbg/mlx5_dbg_dev.c
 *
 * mlx5_dbg device implementation
 */

#ifdef VDURA_CHANGES

#include "mlx5_dbg_io.h"

#define DEVICE_NAME "mlx5dbg"

#ifdef MLX5DBG_FREEBSD
#define printk(...)   printf(__VA_ARGS__)
typedef caddr_t user_data_t;
#define COPY_FROM_USER(local, user_arg) \
  ({ \
    memcpy(&local, user_arg, sizeof(local)); \
    0; \
  })
#define COPY_TO_USER(user_arg, local) copyout(&(local), user_arg, sizeof(local))
#define COPY_TO_USER_RES(user_arg, local) ({ memcpy(user_arg, &local, sizeof(local)); 0; })
#endif

#ifdef MLX5DBG_LINUX
typedef unsigned long user_data_t;
#define COPY_FROM_USER(local, user_arg)                            \
  ({                                                               \
    void __user *__uarg = (void __user *)(user_arg);               \
    int __ret = 0;                                                 \
    if (copy_from_user(&(local), __uarg, sizeof(local)))           \
        __ret = -EFAULT;                                           \
    __ret;                                                         \
  })
#define COPY_TO_USER(user_arg, local)                              \
  ({                                                               \
    void __user *__uarg = (void __user *)(user_arg);               \
    int __ret = 0;                                                 \
    if (copy_to_user(__uarg, &(local), sizeof(local)))             \
        __ret = -EFAULT;                                           \
    __ret;                                                         \
  })
#define COPY_TO_USER_RES COPY_TO_USER
#endif

#ifdef MLX5DBG_FREEBSD
static int
mlx5_dbsf_to_core(const struct mlx5_tool_addr *devaddr,
    struct mlx5_core_dev **mdev)
{
  device_t dev;
  struct pci_dev *pdev;

  dev = pci_find_dbsf(devaddr->domain, devaddr->bus, devaddr->slot,
      devaddr->func);
  if (dev == NULL)
    return (ENOENT);
  if (device_get_devclass(dev) != mlx5_core_driver.bsdclass)
    return (EINVAL);
  pdev = device_get_softc(dev);
  *mdev = pci_get_drvdata(pdev);
  if (*mdev == NULL)
    return (ENOENT);
  return (0);
}
#else
static int
mlx5_dbsf_to_core(const struct mlx5_tool_addr *devaddr,
    struct mlx5_core_dev **mdev)
{
  struct pci_dev *pdev;

  /* Find the PCI device by domain:bus:slot:function */
  pdev = pci_get_domain_bus_and_slot(devaddr->domain,
                                     devaddr->bus,
                                     PCI_DEVFN(devaddr->slot, devaddr->func));
  if (!pdev)
    return -ENODEV;

  /* Optional: check that this device is handled by mlx5_core */
  if (!pdev->driver || strcmp(pdev->driver->name, "mlx5_core") != 0) {
      pci_dev_put(pdev);  /* balance refcount */
      return -EINVAL;
  }

  *mdev = pci_get_drvdata(pdev);
  pci_dev_put(pdev);  /* drop reference from pci_get_domain_bus_and_slot() */

  if (!*mdev)
    return -ENODEV;

  return 0;
}
#endif

#ifdef MLX5DBG_FREEBSD
static int
mlx5_dbg_eq_list_fill(struct mlx5_core_dev *dev, struct mlx5_get_eq_list *eq_list)
{
  struct mlx5_eq_table *table = &dev->priv.eq_table;
  struct mlx5_eq *eq;
  int error, i;

  printk(">> mlx5_dbg_eq_list_fill\n");
  // 3 == pages_eq, async_eq, cmd_eq
  eq_list->eq_list_len = table->num_comp_vectors + 3;
  error = COPY_TO_USER(&eq_list->eq_list[0], table->pages_eq.eqn);
  if (error) {
    return error;
  }
  error = COPY_TO_USER(&eq_list->eq_list[1], table->async_eq.eqn);
  if (error) {
    return error;
  }
  error = COPY_TO_USER(&eq_list->eq_list[2], table->cmd_eq.eqn);
  if (error) {
    return error;
  }

  i = 3;
  list_for_each_entry(eq, &table->comp_eqs_list, list) {
    error = COPY_TO_USER(&eq_list->eq_list[i++], eq->eqn);
    if (error) {
      return error;
    }
  }
  printk("<< mlx5_dbg_eq_list_fill\n");
  return error;
}
#else
struct mlx5_eq_table {
  struct xarray           comp_eqs;
  struct mlx5_eq_async    pages_eq;
  struct mlx5_eq_async    cmd_eq;
  struct mlx5_eq_async    async_eq;

  struct atomic_notifier_head nh[MLX5_EVENT_TYPE_MAX];

  /* Since CQ DB is stored in async_eq */
  struct mlx5_nb          cq_err_nb;

  struct mutex            lock; /* sync async eqs creations */
  struct mutex            comp_lock; /* sync comp eqs creations */
  int     curr_comp_eqs;
  int     max_comp_eqs;
  struct mlx5_irq_table *irq_table;
  struct xarray           comp_irqs;
  struct mlx5_irq         *ctrl_irq;
  struct cpu_rmap   *rmap;
  struct cpumask          used_cpus;
};

static int
mlx5_dbg_eqs_get(struct mlx5_core_dev *dev, struct mlx5_eq **eqs, int len)
{
  struct mlx5_eq_table *table = dev->priv.eq_table;
  struct mlx5_eq_comp *eq;
  unsigned long index;
  int ret = 0;

  if (len > 3) {
    eqs[ret++] = &table->pages_eq.core;
    eqs[ret++] = &table->async_eq.core;
    eqs[ret++] = &table->cmd_eq.core;
  } else {
    printk("Too small eqs size %d\n", len);
    return -ENOSPC;
  }

  xa_for_each(&table->comp_eqs, index, eq) {
    if (ret < len) {
      eqs[ret++] = &eq->core;
    } else {
      printk("Too small eqs size %d, already got %d\n", len, ret);
      return ret;
    }
  }
  return ret;
}

static int
mlx5_dbg_eq_list_fill(struct mlx5_core_dev *dev, struct mlx5_get_eq_list *eq_list)
{
  int error, i, eqs_nr;
  struct mlx5_eq **eqs;

  printk(">> mlx5_dbg_eq_list_fill\n");
  eqs = kzalloc(sizeof(struct mlx5_eq *) * 64, GFP_KERNEL);
  eqs_nr = mlx5_dbg_eqs_get(dev, eqs, 64);
  if (eqs_nr > 0) {
    eq_list->eq_list_len = eqs_nr;

    for (i = 0; i < eq_list->eq_list_len; i++) {
      error = COPY_TO_USER(&eq_list->eq_list[i], eqs[i]->eqn);
      if (error) {
          break;
      }
    }
  }
  printk("<< mlx5_dbg_eq_list_fill\n");
  kfree(eqs);
  return error;
}
#endif

static int
mlx5_dbg_eq_info_copyout(struct mlx5_core_dev *dev, uint32_t *out, struct mlx5_get_eq_info *eq_info)
{
  uint32_t num_eqes;
  uint32_t intr;
  uint32_t log_pg_sz;
  void *ctx;
  int error = 0;

  printk(">> mlx5_dbg_eq_info_copyout\n");

  eq_info->status2 = MLX5_GET(query_eq_out, out, status);
  eq_info->syndrome = MLX5_GET(query_eq_out, out, syndrome);
  eq_info->event_bitmask = MLX5_GET64(query_eq_out, out, event_bitmask);
  ctx = MLX5_ADDR_OF(query_eq_out, out, eq_context_entry);

  num_eqes = 1 << MLX5_GET(eqc, ctx, log_eq_size);
  intr = MLX5_GET(eqc, ctx, intr);
  log_pg_sz = MLX5_GET(eqc, ctx, log_page_size) + 12;
  printk("num_eqes %u, intr %u, log_pg_sz %u\n", num_eqes, intr, log_pg_sz);
  eq_info->num_eqes = num_eqes;
  eq_info->intr = intr;
  eq_info->log_pg_sz = log_pg_sz;
  eq_info->status = MLX5_GET(eqc, ctx, status);
  eq_info->ec = MLX5_GET(eqc, ctx, ec);
  eq_info->oi = MLX5_GET(eqc, ctx, oi);
  eq_info->st = MLX5_GET(eqc, ctx, st);
  eq_info->page_offset = MLX5_GET(eqc, ctx, page_offset);
  eq_info->uar_page = MLX5_GET(eqc, ctx, uar_page);
  eq_info->consumer_counter = MLX5_GET(eqc, ctx, consumer_counter);
  eq_info->producer_counter = MLX5_GET(eqc, ctx, producer_counter);
  printk("<< mlx5_dbg_eq_info_copyout %d\n", error);
  return error;
}

#ifdef MLX5DBG_FREEBSD
static int
mlx5_dbg_cq_list_copyout(struct mlx5_core_dev *dev, struct mlx5_get_cq_list *cq_list)
{
  struct mlx5_cq_table *table = &dev->priv.cq_table;
  int error, i;
  size_t cnt = 0;

  printk(">> mlx5_dbg_cq_list_copyout\n");
  for (i = 0; i < MLX5_CQ_LINEAR_ARRAY_SIZE; i++) {
    if (table->linear_array[i].cq != NULL) {
      error = COPY_TO_USER(&cq_list->cq_list[cnt++], (table->linear_array[i].cq)->cqn);
      if (error) {
        printk("copyout error mlx5_dbg_cq_list_copyout %d %zu\n", i, cnt);
        return error;
      } else {
        printk("copied cq %d, cnt %zu\n", (table->linear_array[i].cq)->cqn, cnt);
      }
    }
  }
  cq_list->cq_list_len = cnt;
  printk("<< mlx5_dbg_cq_list_copyout\n");
  return 0;
}
#else
static int
mlx5_dbg_cq_list_copyout(struct mlx5_core_dev *dev, struct mlx5_get_cq_list *cq_list)
{
  struct mlx5_eq **eqs;
  int eqs_nr;
  int error = 0;
  int i, j = 0;

  printk(">> mlx5_dbg_cq_list_copyout\n");
  eqs = kzalloc(sizeof(struct mlx5_eq *) * 64, GFP_KERNEL);
  eqs_nr = mlx5_dbg_eqs_get(dev, eqs, 64);
  if (eqs_nr > 0) {
    for (i = 0; i < eqs_nr; i++) {
      struct mlx5_cq_table *table = &(eqs[i]->cq_table);
      struct radix_tree_iter iter;
      void **slot;

      spin_lock(&table->lock);

      rcu_read_lock();
      radix_tree_for_each_slot(slot, &table->tree, &iter, 0) {
        struct mlx5_core_cq *entry = rcu_dereference_raw(*slot);

        if (!entry)
            continue;

        error = COPY_TO_USER(&cq_list->cq_list[j++], entry->cqn);
        if (error) {
            break;
        }
      }
      rcu_read_unlock();

      spin_unlock(&table->lock);
    }
  }
  cq_list->cq_list_len = j;
  printk("<< mlx5_dbg_cq_list_copyout\n");
  kfree(eqs);
  return error;
}
#endif

static int
mlx5_dbg_cq_info_copyout(struct mlx5_core_dev *dev, uint32_t *out, struct mlx5_get_cq_info *cq_info)
{
  void *ctx;
  int error = 0;

  printk(">> mlx5_dbg_cq_info_copyout\n");

  ctx = MLX5_ADDR_OF(query_cq_out, out, cq_context);

  cq_info->log_cq_size = 1 << MLX5_GET(cqc, ctx, log_cq_size);
  cq_info->status = MLX5_GET(cqc, ctx, status);
  cq_info->dbr_umem_valid = MLX5_GET(cqc, ctx, dbr_umem_valid);
  cq_info->cqe_sz = MLX5_GET(cqc, ctx, cqe_sz);
  cq_info->cc = MLX5_GET(cqc, ctx, cc);
  cq_info->scqe_break_moderation_en = MLX5_GET(cqc, ctx, scqe_break_moderation_en);
  cq_info->oi = MLX5_GET(cqc, ctx, oi);
  cq_info->cq_period_mode = MLX5_GET(cqc, ctx, cq_period_mode);
#ifdef MLX5DBG_FREEBSD
  cq_info->cqe_compression_en = MLX5_GET(cqc, ctx, cqe_compression_en);
#else
  cq_info->cqe_compression_en = MLX5_GET(cqc, ctx, cqe_comp_en);
#endif
  cq_info->mini_cqe_res_format = MLX5_GET(cqc, ctx, mini_cqe_res_format);
  cq_info->st = MLX5_GET(cqc, ctx, st);
  cq_info->page_offset = MLX5_GET(cqc, ctx, page_offset);
  cq_info->log_cq_size = MLX5_GET(cqc, ctx, log_cq_size);
  cq_info->uar_page = MLX5_GET(cqc, ctx, uar_page);
  cq_info->cq_period = MLX5_GET(cqc, ctx, cq_period);
  cq_info->cq_max_count = MLX5_GET(cqc, ctx, cq_max_count);
#ifdef MLX5DBG_FREEBSD
  cq_info->c_eqn = MLX5_GET(cqc, ctx, c_eqn);
#else
  cq_info->c_eqn = MLX5_GET(cqc, ctx, c_eqn_or_apu_element);
#endif
  cq_info->log_page_size = MLX5_GET(cqc, ctx, log_page_size);
  cq_info->last_notified_index = MLX5_GET(cqc, ctx, last_notified_index);
  cq_info->consumer_counter = MLX5_GET(cqc, ctx, consumer_counter);
  cq_info->producer_counter = MLX5_GET(cqc, ctx, producer_counter);
  cq_info->dbr_addr = MLX5_GET64(cqc, ctx, dbr_addr);
  printk("<< mlx5_dbg_cq_info_copyout %d\n", error);
  return error;
}

int mlx5_core_eq_query_by_num(struct mlx5_core_dev *dev, u8 eqn,u32 *out, int outlen)
{
  u32 in[MLX5_ST_SZ_DW(query_eq_in)] = {0};

  memset(out, 0, outlen);
  MLX5_SET(query_eq_in, in, opcode, MLX5_CMD_OP_QUERY_EQ);
  MLX5_SET(query_eq_in, in, eq_number, eqn);

  return mlx5_cmd_exec(dev, in, sizeof(in), out, outlen);
}

int mlx5_core_query_cq_by_num(struct mlx5_core_dev *dev, u32 cqn, u32 *out, int outlen)
{
  u32 in[MLX5_ST_SZ_DW(query_cq_in)] = {0};

  MLX5_SET(query_cq_in, in, opcode, MLX5_CMD_OP_QUERY_CQ);
  MLX5_SET(query_cq_in, in, cqn, cqn);

  return mlx5_cmd_exec(dev, in, sizeof(in), out, outlen);
}

static int
mlx5_dbg_query_tir(struct mlx5_core_dev *dev, u8 tirn, struct mlx5_get_tir_info *tir_info)
{
  u32 in[MLX5_ST_SZ_DW(query_tir_in)] = {0};
  uint32_t *out;
  void *ctx;
  int ret;

  out = kzalloc(1024, GFP_KERNEL);

  MLX5_SET(query_tir_in, in, opcode, MLX5_CMD_OP_QUERY_TIR);
  MLX5_SET(query_tir_in, in, tirn, tirn);

  ret = mlx5_cmd_exec(dev, in, sizeof(in), out, 1024);
  if (ret == 0) {
    ctx = MLX5_ADDR_OF(query_tir_out, out, tir_context);
    tir_info->disp_type = MLX5_GET(tirc, ctx, disp_type);
    tir_info->tls_en = MLX5_GET(tirc, ctx, tls_en);
    tir_info->lro_timeout_period_usecs = MLX5_GET(tirc, ctx, lro_timeout_period_usecs);
#ifdef MLX5DBG_FREEBSD
    tir_info->lro_enable_mask = MLX5_GET(tirc, ctx, lro_enable_mask);
    tir_info->lro_max_msg_sz = MLX5_GET(tirc, ctx, lro_max_msg_sz);
#else
    tir_info->lro_enable_mask = MLX5_GET(tirc, ctx, packet_merge_mask);
    tir_info->lro_max_msg_sz = MLX5_GET(tirc, ctx, lro_max_ip_payload_size);
#endif
    tir_info->inline_rqn = MLX5_GET(tirc, ctx, inline_rqn);
    tir_info->rx_hash_symmetric = MLX5_GET(tirc, ctx, rx_hash_symmetric);
    tir_info->tunneled_offload_en = MLX5_GET(tirc, ctx, tunneled_offload_en);
    tir_info->indirect_table = MLX5_GET(tirc, ctx, indirect_table);
    tir_info->rx_hash_fn = MLX5_GET(tirc, ctx, rx_hash_fn);
#ifdef MLX5DBG_FREEBSD
    tir_info->self_lb_en = MLX5_GET(tirc, ctx, self_lb_en);
#else
    tir_info->self_lb_en = MLX5_GET(tirc, ctx, self_lb_block);
#endif
    tir_info->transport_domain = MLX5_GET(tirc, ctx, transport_domain);
  }

  kfree(out);
  return ret;
}

static int
mlx5_dbg_tir_list_copyout(struct mlx5_core_dev *dev, struct mlx5_get_tir_list *tir_list)
{
  int error, ret, i;
  struct mlx5_get_tir_info tir_info;
  size_t cnt = 0;

  printk(">> mlx5_dbg_tir_list_copyout\n");
  for (i = 0; i < 32; i++) {
    ret = mlx5_dbg_query_tir(dev, i, &tir_info);
    if (ret == 0) {
      error = COPY_TO_USER(&tir_list->tir_list[cnt++], i);
      if (error) {
        printk("copyout error mlx5_dbg_tir_list_copyout %d %zu\n", i, cnt);
        return error;
      } else {
        printk("copied tir %d, cnt %zu\n", i, cnt);
      }
    }
  }
  tir_list->tir_list_len = cnt;
  printk("<< mlx5_dbg_tir_list_copyout\n");
  return 0;
}

static int
mlx5_dbg_query_rqt(struct mlx5_core_dev *dev, u8 rqtn, struct mlx5_get_rqt_info *rqt_info)
{
  u32 in[MLX5_ST_SZ_DW(query_rqt_in)] = {0};
  uint32_t *out;
  void *ctx;
  int ret, i;

  out = kzalloc(1024, GFP_KERNEL);

  MLX5_SET(query_rqt_in, in, opcode, MLX5_CMD_OP_QUERY_RQT);
  MLX5_SET(query_rqt_in, in, rqtn, rqtn);

  ret = mlx5_cmd_exec(dev, in, sizeof(in), out, 1024);
  if (ret == 0) {
    ctx = MLX5_ADDR_OF(query_rqt_out, out, rqt_context);
    rqt_info->rqt_max_size = MLX5_GET(rqtc, ctx, rqt_max_size);
    rqt_info->rqt_actual_size = MLX5_GET(rqtc, ctx, rqt_actual_size);
    for (i = 0; i < rqt_info->rqt_actual_size; i++) {
      rqt_info->rq_num[i] = MLX5_GET(rqtc, ctx, rq_num[i]);
    }
  }

  kfree(out);
  return ret;
}

static int
mlx5_dbg_rqt_list_copyout(struct mlx5_core_dev *dev, struct mlx5_get_rqt_list *rqt_list)
{
  int error, ret, i;
  struct mlx5_get_rqt_info rqt_info;
  size_t cnt = 0;

  printk(">> mlx5_dbg_rqt_list_copyout\n");
  for (i = 0; i < 32; i++) {
    ret = mlx5_dbg_query_rqt(dev, i, &rqt_info);
    if (ret == 0) {
      error = COPY_TO_USER(&rqt_list->rqt_list[cnt++], i);
      if (error) {
        printk("copyout error mlx5_dbg_rqt_list_copyout %d %zu\n", i, cnt);
        return error;
      } else {
        printk("copied rqt %d, cnt %zu\n", i, cnt);
      }
    }
  }
  rqt_list->rqt_list_len = cnt;
  printk("<< mlx5_dbg_rqt_list_copyout\n");
  return 0;
}

static int
mlx5_dbg_query_qp(struct mlx5_core_dev *dev, uint32_t qpn, struct mlx5_get_qp_info *qp_info)
{
  u32 in[MLX5_ST_SZ_DW(query_qp_in)] = {0};
  uint32_t *out;
  void *ctx;
  int ret;

  out = kzalloc(2048, GFP_KERNEL);

  MLX5_SET(query_qp_in, in, opcode, MLX5_CMD_OP_QUERY_QP);
  MLX5_SET(query_qp_in, in, qpn, qpn);

  ret = mlx5_cmd_exec(dev, in, sizeof(in), out, 2048);
  if (ret == 0) {
    ctx = MLX5_ADDR_OF(query_qp_out, out, qpc);
    qp_info->state = MLX5_GET(qpc, ctx, state);
    qp_info->lag_tx_port_affinity = MLX5_GET(qpc, ctx, lag_tx_port_affinity);
    qp_info->st = MLX5_GET(qpc, ctx, st);
#ifdef MLX5DBG_LINUX
    qp_info->isolate_vl_tc = MLX5_GET(qpc, ctx, isolate_vl_tc);
    qp_info->req_e2e_credit_mode = MLX5_GET(qpc, ctx, req_e2e_credit_mode);
    qp_info->offload_type = MLX5_GET(qpc, ctx, offload_type);
#endif
    qp_info->end_padding_mode = MLX5_GET(qpc, ctx, end_padding_mode);
    qp_info->wq_signature = MLX5_GET(qpc, ctx, wq_signature);
    qp_info->block_lb_mc = MLX5_GET(qpc, ctx, block_lb_mc);
    qp_info->atomic_like_write_en = MLX5_GET(qpc, ctx, atomic_like_write_en);
    qp_info->latency_sensitive = MLX5_GET(qpc, ctx, latency_sensitive);
    qp_info->drain_sigerr = MLX5_GET(qpc, ctx, drain_sigerr);
    qp_info->pd = MLX5_GET(qpc, ctx, pd);
    qp_info->mtu = MLX5_GET(qpc, ctx, mtu);
    qp_info->log_msg_max = MLX5_GET(qpc, ctx, log_msg_max);
    qp_info->log_rq_size = MLX5_GET(qpc, ctx, log_rq_size);
    qp_info->log_rq_stride = MLX5_GET(qpc, ctx, log_rq_stride);
    qp_info->no_sq = MLX5_GET(qpc, ctx, no_sq);
    qp_info->log_sq_size = MLX5_GET(qpc, ctx, log_sq_size);
#ifdef MLX5DBG_LINUX
    qp_info->retry_mode = MLX5_GET(qpc, ctx, retry_mode);
#endif
    qp_info->ts_format = MLX5_GET(qpc, ctx, ts_format);
    qp_info->rlky = MLX5_GET(qpc, ctx, rlky);
    qp_info->ulp_stateless_offload_mode = MLX5_GET(qpc, ctx, ulp_stateless_offload_mode);
    qp_info->counter_set_id = MLX5_GET(qpc, ctx, counter_set_id);
    qp_info->uar_page = MLX5_GET(qpc, ctx, uar_page);
    qp_info->user_index = MLX5_GET(qpc, ctx, user_index);
    qp_info->log_page_size = MLX5_GET(qpc, ctx, log_page_size);
    qp_info->remote_qpn = MLX5_GET(qpc, ctx, remote_qpn);
    qp_info->log_ack_req_freq = MLX5_GET(qpc, ctx, log_ack_req_freq);
    qp_info->log_sra_max = MLX5_GET(qpc, ctx, log_sra_max);
    qp_info->retry_count = MLX5_GET(qpc, ctx, retry_count);
    qp_info->rnr_retry = MLX5_GET(qpc, ctx, rnr_retry);
    qp_info->fre = MLX5_GET(qpc, ctx, fre);
    qp_info->cur_rnr_retry = MLX5_GET(qpc, ctx, cur_rnr_retry);
    qp_info->cur_retry_count = MLX5_GET(qpc, ctx, cur_retry_count);
    qp_info->next_send_psn = MLX5_GET(qpc, ctx, next_send_psn);
#ifdef MLX5DBG_LINUX
    qp_info->log_num_dci_stream_channels = MLX5_GET(qpc, ctx, log_num_dci_stream_channels);
#endif
    qp_info->cqn_snd = MLX5_GET(qpc, ctx, cqn_snd);
#ifdef MLX5DBG_LINUX
    qp_info->log_num_dci_errored_streams = MLX5_GET(qpc, ctx, log_num_dci_errored_streams);
#endif
    qp_info->deth_sqpn = MLX5_GET(qpc, ctx, deth_sqpn);
    qp_info->last_acked_psn = MLX5_GET(qpc, ctx, last_acked_psn);
    qp_info->ssn = MLX5_GET(qpc, ctx, ssn);
    qp_info->log_rra_max = MLX5_GET(qpc, ctx, log_rra_max);
    qp_info->atomic_mode = MLX5_GET(qpc, ctx, atomic_mode);
    qp_info->rre = MLX5_GET(qpc, ctx, rre);
    qp_info->rwe = MLX5_GET(qpc, ctx, rwe);
    qp_info->rae = MLX5_GET(qpc, ctx, rae);
    qp_info->page_offset = MLX5_GET(qpc, ctx, page_offset);
    qp_info->cd_slave_receive = MLX5_GET(qpc, ctx, cd_slave_receive);
    qp_info->cd_slave_send = MLX5_GET(qpc, ctx, cd_slave_send);
    qp_info->cd_master = MLX5_GET(qpc, ctx, cd_master);
    qp_info->min_rnr_nak = MLX5_GET(qpc, ctx, min_rnr_nak);
    qp_info->next_rcv_psn = MLX5_GET(qpc, ctx, next_rcv_psn);
    qp_info->xrcd = MLX5_GET(qpc, ctx, xrcd);
    qp_info->cqn_rcv = MLX5_GET(qpc, ctx, cqn_rcv);
    qp_info->dbr_addr = MLX5_GET64(qpc, ctx, dbr_addr);
    qp_info->q_key = MLX5_GET(qpc, ctx, q_key);
    qp_info->rq_type = MLX5_GET(qpc, ctx, rq_type);
#ifdef MLX5DBG_LINUX
    qp_info->srqn_rmpn_xrqn = MLX5_GET(qpc, ctx, srqn_rmpn_xrqn);
#else
    qp_info->srqn_rmpn_xrqn = MLX5_GET(qpc, ctx, srqn_rmpn);
#endif
    qp_info->rmsn = MLX5_GET(qpc, ctx, rmsn);
    qp_info->hw_sq_wqebb_counter = MLX5_GET(qpc, ctx, hw_sq_wqebb_counter);
    qp_info->sw_sq_wqebb_counter = MLX5_GET(qpc, ctx, sw_sq_wqebb_counter);
    qp_info->hw_rq_counter = MLX5_GET(qpc, ctx, hw_rq_counter);
    qp_info->sw_rq_counter = MLX5_GET(qpc, ctx, sw_rq_counter);
    qp_info->cgs = MLX5_GET(qpc, ctx, cgs);
    qp_info->cs_req = MLX5_GET(qpc, ctx, cs_req);
    qp_info->cs_res = MLX5_GET(qpc, ctx, cs_res);
    qp_info->dc_access_key = MLX5_GET64(qpc, ctx, dc_access_key);
    qp_info->dbr_umem_valid = MLX5_GET(qpc, ctx, dbr_umem_valid);
  }

  kfree(out);
  return ret;
}

static void
mlx5_dbg_copy_outer_match_params(void *mc, struct match_params *mp)
{
  mp->smac_47_16 = MLX5_GET(fte_match_set_lyr_2_4, mc, smac_47_16);
  mp->smac_15_0 = MLX5_GET(fte_match_set_lyr_2_4, mc, smac_15_0);
  mp->ethertype = MLX5_GET(fte_match_set_lyr_2_4, mc, ethertype);
  mp->dmac_47_16 = MLX5_GET(fte_match_set_lyr_2_4, mc, dmac_47_16);
  mp->dmac_15_0 = MLX5_GET(fte_match_set_lyr_2_4, mc, dmac_15_0);
  mp->first_prio = MLX5_GET(fte_match_set_lyr_2_4, mc, first_prio);
  mp->first_cfi = MLX5_GET(fte_match_set_lyr_2_4, mc, first_cfi);
  mp->first_vid = MLX5_GET(fte_match_set_lyr_2_4, mc, first_vid);
  mp->ip_protocol = MLX5_GET(fte_match_set_lyr_2_4, mc, ip_protocol);
  mp->ip_dscp = MLX5_GET(fte_match_set_lyr_2_4, mc, ip_dscp);
  mp->ip_ecn = MLX5_GET(fte_match_set_lyr_2_4, mc, ip_ecn);
  mp->cvlan_tag = MLX5_GET(fte_match_set_lyr_2_4, mc, cvlan_tag);
  mp->svlan_tag = MLX5_GET(fte_match_set_lyr_2_4, mc, svlan_tag);
  mp->frag = MLX5_GET(fte_match_set_lyr_2_4, mc, frag);
  mp->ip_version = MLX5_GET(fte_match_set_lyr_2_4, mc, ip_version);
  mp->tcp_flags = MLX5_GET(fte_match_set_lyr_2_4, mc, tcp_flags);
  mp->tcp_sport = MLX5_GET(fte_match_set_lyr_2_4, mc, tcp_sport);
  mp->tcp_dport = MLX5_GET(fte_match_set_lyr_2_4, mc, tcp_dport);
  mp->udp_sport = MLX5_GET(fte_match_set_lyr_2_4, mc, udp_sport);
  mp->udp_dport = MLX5_GET(fte_match_set_lyr_2_4, mc, udp_dport);
  memcpy(&mp->src_ipv4_src_ipv6, MLX5_ADDR_OF(fte_match_set_lyr_2_4, mc, src_ipv4_src_ipv6), sizeof(mp->src_ipv4_src_ipv6));
  memcpy(&mp->dst_ipv4_dst_ipv6, MLX5_ADDR_OF(fte_match_set_lyr_2_4, mc, dst_ipv4_dst_ipv6), sizeof(mp->dst_ipv4_dst_ipv6));
}

static void
mlx5_dbg_copy_misc_match_params(void *mc, struct match_params *mp)
{
  mp->source_sqn = MLX5_GET(fte_match_set_misc, mc, source_sqn);
  mp->source_port = MLX5_GET(fte_match_set_misc, mc, source_port);
  mp->outer_second_prio = MLX5_GET(fte_match_set_misc, mc, outer_second_prio);
  mp->outer_second_cfi = MLX5_GET(fte_match_set_misc, mc, outer_second_cfi);
  mp->outer_second_vid = MLX5_GET(fte_match_set_misc, mc, outer_second_vid);
  mp->inner_second_prio = MLX5_GET(fte_match_set_misc, mc, inner_second_prio);
  mp->inner_second_cfi = MLX5_GET(fte_match_set_misc, mc, inner_second_cfi);
  mp->inner_second_vid = MLX5_GET(fte_match_set_misc, mc, inner_second_vid);
#ifdef MLX5DBG_LINUX
  mp->outer_second_vlan_tag = MLX5_GET(fte_match_set_misc, mc, outer_second_svlan_tag);
  mp->inner_second_vlan_tag = MLX5_GET(fte_match_set_misc, mc, inner_second_svlan_tag);
  mp->gre_key_h = MLX5_GET(fte_match_set_misc, mc, gre_key.nvgre.hi);
  mp->gre_key_l = MLX5_GET(fte_match_set_misc, mc, gre_key.nvgre.lo);
#else
  mp->outer_second_vlan_tag = MLX5_GET(fte_match_set_misc, mc, outer_second_vlan_tag);
  mp->inner_second_vlan_tag = MLX5_GET(fte_match_set_misc, mc, inner_second_vlan_tag);
  mp->gre_key_h = MLX5_GET(fte_match_set_misc, mc, gre_key_h);
  mp->gre_key_l = MLX5_GET(fte_match_set_misc, mc, gre_key_l);
#endif
  mp->gre_protocol = MLX5_GET(fte_match_set_misc, mc, gre_protocol);
  mp->vxlan_vni = MLX5_GET(fte_match_set_misc, mc, vxlan_vni);
  mp->geneve_vni = MLX5_GET(fte_match_set_misc, mc, geneve_vni);
  mp->geneve_oam = MLX5_GET(fte_match_set_misc, mc, geneve_oam);
  mp->outer_ipv6_flow_label = MLX5_GET(fte_match_set_misc, mc, outer_ipv6_flow_label);
  mp->inner_ipv6_flow_label = MLX5_GET(fte_match_set_misc, mc, inner_ipv6_flow_label);
  mp->geneve_opt_len = MLX5_GET(fte_match_set_misc, mc, geneve_opt_len);
  mp->geneve_protocol_type = MLX5_GET(fte_match_set_misc, mc, geneve_protocol_type);
  mp->bth_dst_qp = MLX5_GET(fte_match_set_misc, mc, bth_dst_qp);
}

static int
mlx5_dbg_query_fte(struct mlx5_core_dev *dev, uint32_t ft_id, uint32_t flow_index, uint32_t match_type, struct mlx5_get_fte_info *fte_info)
{
  u32 in[MLX5_ST_SZ_DW(query_fte_in)] = {0};
  uint32_t *out;
  void *fc, *mc;
  void *dest, *flow_counter;
  int i;
  int ret;

  out = kzalloc(1024, GFP_KERNEL);

  MLX5_SET(query_fte_in, in, opcode, MLX5_CMD_OP_QUERY_FLOW_TABLE_ENTRY);
  MLX5_SET(query_fte_in, in, table_type, FS_FT_NIC_RX);
  MLX5_SET(query_fte_in, in, table_id, ft_id);
  MLX5_SET(query_fte_in, in, flow_index, flow_index);

  ret = mlx5_cmd_exec(dev, in, sizeof(in), out, 1024);
  if (ret == 0) {
    fc = MLX5_ADDR_OF(query_fte_out, out, flow_context);
    fte_info->group_id = MLX5_GET(flow_context, fc, group_id);
    fte_info->flow_tag = MLX5_GET(flow_context, fc, flow_tag);
    fte_info->action = MLX5_GET(flow_context, fc, action);
    fte_info->destination_list_size = MLX5_GET(flow_context, fc, destination_list_size);
    if (fte_info->destination_list_size > MAX_DESTINATIONS_NR) {
      printk("warn: destination_list_size %u is bigger than max %u",
          fte_info->destination_list_size, MAX_DESTINATIONS_NR);
      fte_info->destination_list_size = MAX_DESTINATIONS_NR;
    }
    fte_info->flow_counter_list_size = MLX5_GET(flow_context, fc, flow_counter_list_size);
    if (fte_info->flow_counter_list_size > MAX_FLOW_COUNTER_NR) {
      printk("warn: flow_counter_list_size %u is bigger than max %u",
          fte_info->flow_counter_list_size, MAX_FLOW_COUNTER_NR);
      fte_info->flow_counter_list_size = MAX_FLOW_COUNTER_NR;
    }
    fte_info->packet_reformat_id = MLX5_GET(flow_context, fc, packet_reformat_id);
    if (match_type == MLX5_MATCH_CRITERIA_OUTER_HEADERS) {
      mc = MLX5_ADDR_OF(flow_context, fc, match_value.outer_headers);
      mlx5_dbg_copy_outer_match_params(mc, &fte_info->match_value);
    } else if (match_type == MLX5_MATCH_CRITERIA_MISC_PARAMS) {
      mc = MLX5_ADDR_OF(flow_context, fc, match_value.misc_parameters);
      mlx5_dbg_copy_misc_match_params(mc, &fte_info->match_value);
    }
    dest = MLX5_ADDR_OF(flow_context, fc, destination);
    for (i = 0; i < fte_info->destination_list_size; i++) {
      fte_info->destinations[i].destination_type = MLX5_GET(dest_format_struct, dest, destination_type);
      fte_info->destinations[i].destination_id = MLX5_GET(dest_format_struct, dest, destination_id);
      fte_info->destinations[i].destination_table_type = MLX5_GET(dest_format_struct, dest, destination_table_type);
      dest = (void *)((char *)dest + MLX5_ST_SZ_DW(dest_format_struct));
    }
    flow_counter = dest;
    for (i = 0; i < fte_info->flow_counter_list_size; i++) {
      fte_info->flow_counters[i].flow_counter_id = MLX5_GET(flow_counter_list, flow_counter, flow_counter_id);
      flow_counter = (void *)((char *)flow_counter + MLX5_ST_SZ_DW(flow_counter_list));
    }
  }

  return ret;
}

static void
mlx5_dbg_query_ft_groups(struct mlx5_core_dev *dev, uint32_t ft_id, struct mlx5_get_ft_info *ft_info)
{
  u32 in[MLX5_ST_SZ_DW(query_flow_group_in)] = {0};
  uint32_t *out;
  void *mc;
  int i, cnt = 0;
  int ret;
  struct mlx5_fg_info *fgs = &ft_info->flow_groups[0];

  out = kzalloc(1024, GFP_KERNEL);

  MLX5_SET(query_flow_group_in, in, opcode, MLX5_CMD_OP_QUERY_FLOW_GROUP);
  MLX5_SET(query_flow_group_in, in, table_type, FS_FT_NIC_RX);
  MLX5_SET(query_flow_group_in, in, table_id, ft_id);

  for (i = 0; i < MLX5_MAX_FG_CNT; i++) { 
    MLX5_SET(query_flow_group_in, in, group_id, i);
    ret = mlx5_cmd_exec(dev, in, sizeof(in), out, 1024);
    if (ret == 0) {
      fgs[cnt].group_id = i;
      fgs[cnt].start_flow_index = MLX5_GET(query_flow_group_out, out, start_flow_index);
      fgs[cnt].end_flow_index = MLX5_GET(query_flow_group_out, out, end_flow_index);
      fgs[cnt].match_criteria_enable = MLX5_GET(query_flow_group_out, out, match_criteria_enable);
      if (fgs[cnt].match_criteria_enable == MLX5_MATCH_CRITERIA_OUTER_HEADERS) {
        mc = MLX5_ADDR_OF(query_flow_group_out, out, match_criteria.outer_headers);
        mlx5_dbg_copy_outer_match_params(mc, &fgs[cnt].mp);
      } else if (fgs[cnt].match_criteria_enable == MLX5_MATCH_CRITERIA_MISC_PARAMS) {
        mc = MLX5_ADDR_OF(query_flow_group_out, out, match_criteria.misc_parameters);
        mlx5_dbg_copy_misc_match_params(mc, &fgs[cnt].mp);
      }
      cnt++;
    }
  }
  ft_info->flow_groups_cnt = cnt;

  kfree(out);
}

static int
mlx5_dbg_query_ft(struct mlx5_core_dev *dev, uint32_t ft_id, struct mlx5_get_ft_info *ft_info)
{
  u32 in[MLX5_ST_SZ_DW(query_flow_table_in)] = {0};
  uint32_t *out;
  void *ctx;
  int ret;

  out = kzalloc(1024, GFP_KERNEL);

  MLX5_SET(query_flow_table_in, in, opcode, MLX5_CMD_OP_QUERY_FLOW_TABLE);
  MLX5_SET(query_flow_table_in, in, table_type, FS_FT_NIC_RX);
  MLX5_SET(query_flow_table_in, in, table_id, ft_id);

  ret = mlx5_cmd_exec(dev, in, sizeof(in), out, 1024);
  if (ret == 0) {
    ctx = MLX5_ADDR_OF(query_flow_table_out, out, flow_table_context);
    ft_info->reformat_en = MLX5_GET(flow_table_context, ctx, reformat_en);
    ft_info->decap_en = MLX5_GET(flow_table_context, ctx, decap_en);
    ft_info->table_miss_action = MLX5_GET(flow_table_context, ctx, table_miss_action);
    ft_info->level = MLX5_GET(flow_table_context, ctx, level);
    ft_info->log_size = MLX5_GET(flow_table_context, ctx, log_size);
    ft_info->table_miss_id = MLX5_GET(flow_table_context, ctx, table_miss_id);
    ft_info->lag_master_next_table_id = MLX5_GET(flow_table_context, ctx, lag_master_next_table_id);
#ifdef MLX5DBG_LINUX
    ft_info->sw_owner = MLX5_GET(flow_table_context, ctx, sw_owner);
    ft_info->termination_table = MLX5_GET(flow_table_context, ctx, termination_table);
    ft_info->sw_owner_icm_root_1 = MLX5_GET64(flow_table_context, ctx, sw_owner_icm_root_1);
    ft_info->sw_owner_icm_root_0 = MLX5_GET64(flow_table_context, ctx, sw_owner_icm_root_0);
#endif
    mlx5_dbg_query_ft_groups(dev, ft_id, ft_info);
  }

  kfree(out);
  return ret;
}

static int
mlx5_dbg_query_hca_cap(struct mlx5_core_dev *dev, struct mlx5_get_hca_cap *hca_cap)
{
  u32 in[MLX5_ST_SZ_DW(query_hca_cap_in)] = {0};
  uint32_t *out;
  uint16_t op_mode = (MLX5_CAP_GENERAL << 1) | (HCA_CAP_OPMOD_GET_CUR & 0x01);
  void *ctx;
  int ret;

  out = kzalloc(1024, GFP_KERNEL);

  MLX5_SET(query_hca_cap_in, in, opcode, MLX5_CMD_OP_QUERY_HCA_CAP);
  MLX5_SET(query_hca_cap_in, in, op_mod, op_mode);

  ret = mlx5_cmd_exec(dev, in, sizeof(in), out, 1024);
  if (ret == 0) {
    ctx = MLX5_ADDR_OF(query_hca_cap_out, out, capability);
    hca_cap->nic_flow_table = MLX5_GET(cmd_hca_cap, ctx, nic_flow_table);
  }

  kfree(out);
  return ret;
}

static int mlx5_dbg_get_eq_list(user_data_t data)
{
  struct mlx5_core_dev *mdev;
  struct mlx5_get_eq_list eq_list;
  struct mlx5_tool_addr *devaddr;
  int error;

  error = COPY_FROM_USER(eq_list, data);
  if (error != 0)
    return error;
  devaddr = &eq_list.devaddr;
  error = mlx5_dbsf_to_core(devaddr, &mdev);
  if (error != 0)
    return error;
  mlx5_dbg_eq_list_fill(mdev, &eq_list);
  error = COPY_TO_USER_RES(data, eq_list);
  return error;
}

static int mlx5_dbg_get_eq_info(user_data_t data)
{
  struct mlx5_core_dev *mdev;
  struct mlx5_get_eq_info eq_info;
  struct mlx5_tool_addr *devaddr;
  uint32_t *cmd_out;
  int error;

  error = COPY_FROM_USER(eq_info, data);
  if (error != 0)
    return error;
  devaddr = &eq_info.devaddr;
  error = mlx5_dbsf_to_core(devaddr, &mdev);
  if (error != 0)
    return error;
  cmd_out = kzalloc(1024, GFP_KERNEL);
  error = mlx5_core_eq_query_by_num(mdev, eq_info.eqn, cmd_out, 1024);
  if (!error) {
    error = mlx5_dbg_eq_info_copyout(mdev, cmd_out, &eq_info);
    if (!error) {
      error = COPY_TO_USER_RES(data, eq_info);
    }
  } else {
    printk("failed to query eq %hd\n", eq_info.eqn);
  }
  kfree(cmd_out);
  return error;
}

static int
mlx5_dbg_get_fte_info(user_data_t data)
{
  struct mlx5_core_dev *mdev;
  struct mlx5_get_fte_info fte_info;
  int error;

  COPY_FROM_USER(fte_info, data);
  error = mlx5_dbsf_to_core(&fte_info.devaddr, &mdev);
  if (error != 0)
    return error;
  error = mlx5_dbg_query_fte(mdev, fte_info.ft_id, fte_info.flow_index, fte_info.match_type, &fte_info);
  if (error) {
    printk("failed to query Flow Table Entry %u\n", fte_info.flow_index);
  } else {
    error = COPY_TO_USER_RES(data, fte_info);
  }
  return error;
}

#ifdef MLX5DBG_FREEBSD
static int
mlx5_dbg_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
#else
static long mlx5_dbg_ioctl(struct file *file, unsigned int cmd, unsigned long data)
#endif
{
  struct mlx5_core_dev *mdev;
  struct mlx5_get_cq_list cq_list;
  struct mlx5_get_cq_info cq_info;
  struct mlx5_get_tir_list tir_list;
  struct mlx5_get_tir_info tir_info;
  struct mlx5_get_rqt_list rqt_list;
  struct mlx5_get_rqt_info rqt_info;
  struct mlx5_get_qp_info qp_info;
  struct mlx5_get_hca_cap hca_cap;
  struct mlx5_get_ft_info ft_info;
  uint32_t *cmd_out;
  int error;

  error = 0;
  switch (cmd) {
  case MLX5_DBG_GET_EQ_LIST:
    printk("MLX5_DBG_GET_EQ_LIST\n");
    error = mlx5_dbg_get_eq_list(data);
    break;
  case MLX5_DBG_GET_EQ_INFO:
    printk("MLX5_DBG_GET_EQ_INFO\n");
    error = mlx5_dbg_get_eq_info(data);
    break;
  case MLX5_DBG_GET_CQ_LIST:
    printk("MLX5_DBG_GET_CQ_LIST\n");
    COPY_FROM_USER(cq_list, data);
    error = mlx5_dbsf_to_core(&cq_list.devaddr, &mdev);
    if (error != 0)
      break;
    error = mlx5_dbg_cq_list_copyout(mdev, &cq_list);
    if (!error) {
      error = COPY_TO_USER_RES(data, cq_list);
    }
    break;
  case MLX5_DBG_GET_CQ_INFO:
    printk("MLX5_DBG_GET_CQ_INFO\n");
    COPY_FROM_USER(cq_info, data);
    error = mlx5_dbsf_to_core(&cq_info.devaddr, &mdev);
    if (error != 0)
      break;
    cmd_out = kzalloc(1024, GFP_KERNEL);
    error = mlx5_core_query_cq_by_num(mdev, cq_info.cqn, cmd_out, 1024);
    if (!error) {
      error = mlx5_dbg_cq_info_copyout(mdev, cmd_out, &cq_info);
    } else {
      printk("failed to query cq %u\n", cq_info.cqn);
    }
    if (!error) {
      error = COPY_TO_USER_RES(data, cq_info);
    }
    kfree(cmd_out);
    break;
  case MLX5_DBG_GET_TIR_LIST:
    printk("MLX5_DBG_GET_TIR_LIST\n");
    COPY_FROM_USER(tir_list, data);
    error = mlx5_dbsf_to_core(&tir_list.devaddr, &mdev);
    if (error != 0)
      break;
    error = mlx5_dbg_tir_list_copyout(mdev, &tir_list);
    if (!error) {
      error = COPY_TO_USER_RES(data, tir_list);
    }
    break;
  case MLX5_DBG_GET_TIR_INFO:
    printk("MLX5_DBG_GET_TIR_INFO\n");
    COPY_FROM_USER(tir_info, data);
    error = mlx5_dbsf_to_core(&tir_info.devaddr, &mdev);
    if (error != 0)
      break;
    error = mlx5_dbg_query_tir(mdev, tir_info.tirn, &tir_info);
    if (error) {
      printk("failed to query tir %u\n", tir_info.tirn);
    } else {
      error = COPY_TO_USER_RES(data, tir_info);
    }
    break;
  case MLX5_DBG_GET_RQT_LIST:
    printk("MLX5_DBG_GET_RQT_LIST\n");
    COPY_FROM_USER(rqt_list, data);
    error = mlx5_dbsf_to_core(&rqt_list.devaddr, &mdev);
    if (error != 0)
      break;
    error = mlx5_dbg_rqt_list_copyout(mdev, &rqt_list);
    if (!error) {
      error = COPY_TO_USER_RES(data, rqt_list);
    }
    break;
  case MLX5_DBG_GET_RQT_INFO:
    printk("MLX5_DBG_GET_RQT_INFO\n");
    COPY_FROM_USER(rqt_info, data);
    error = mlx5_dbsf_to_core(&rqt_info.devaddr, &mdev);
    if (error != 0)
      break;
    error = mlx5_dbg_query_rqt(mdev, rqt_info.rqtn, &rqt_info);
    if (error) {
      printk("failed to query rqt %u\n", rqt_info.rqtn);
    } else {
      error = COPY_TO_USER_RES(data, rqt_info);
    }
    break;
  case MLX5_DBG_GET_QP_INFO:
    printk("MLX5_DBG_GET_QP_INFO\n");
    COPY_FROM_USER(qp_info, data);
    error = mlx5_dbsf_to_core(&qp_info.devaddr, &mdev);
    if (error != 0)
      break;
    error = mlx5_dbg_query_qp(mdev, qp_info.qpn, &qp_info);
    if (error) {
      printk("failed to query qp %u\n", qp_info.qpn);
    } else {
      error = COPY_TO_USER_RES(data, qp_info);
    }
    break;
  case MLX5_DBG_GET_HCA_CAP:
    printk("MLX5_DBG_GET_HCA_CAP\n");
    COPY_FROM_USER(hca_cap, data);
    error = mlx5_dbsf_to_core(&hca_cap.devaddr, &mdev);
    if (error != 0)
      break;
    error = mlx5_dbg_query_hca_cap(mdev, &hca_cap);
    if (error) {
      printk("failed to query HCA Capabilities\n");
    } else {
      error = COPY_TO_USER_RES(data, hca_cap);
    }
    break;
  case MLX5_DBG_GET_FT_INFO:
    printk("MLX5_DBG_GET_FT_INFO\n");
    COPY_FROM_USER(ft_info, data);
    error = mlx5_dbsf_to_core(&ft_info.devaddr, &mdev);
    if (error != 0)
      break;
    error = mlx5_dbg_query_ft(mdev, ft_info.ft_id, &ft_info);
    if (error) {
      printk("failed to query Flow Table %u\n", ft_info.ft_id);
    } else {
      error = COPY_TO_USER_RES(data, ft_info);
    }
    break;
  case MLX5_DBG_GET_FTE_INFO:
    printk("MLX5_DBG_GET_FTE_INFO\n");
    error = mlx5_dbg_get_fte_info(data);
    break;
  default:
    error = ENOTTY;
    break;
  }
  return (error);
}

#ifdef MLX5DBG_FREEBSD
static struct cdevsw mlx5_dbg_devsw = {
  .d_version =  D_VERSION,
  .d_ioctl =  mlx5_dbg_ioctl,
};

struct cdev *mlx5_dbg_dev;

int mlx5_init_dbg_dev(void);

int mlx5_init_dbg_dev(void)
{
  struct make_dev_args mda;
  int error;

  make_dev_args_init(&mda);
  mda.mda_flags = MAKEDEV_WAITOK | MAKEDEV_CHECKNAME;
  mda.mda_devsw = &mlx5_dbg_devsw;
  mda.mda_uid = UID_ROOT;
  mda.mda_gid = GID_OPERATOR;
  mda.mda_mode = 0640;
  error = make_dev_s(&mda, &mlx5_dbg_dev, DEVICE_NAME);
  return (-error);
}

static void mlx5_dbg_dev_fini(void)
{
  if (mlx5_dbg_dev != NULL)
    destroy_dev(mlx5_dbg_dev);
}

#else /* MLX5DBG_FREEBSD */

static dev_t mlx5_dbg_devno;
static struct cdev mlx5_dbg_cdev;
static struct class *mlx5_dbg_class;

/* file operations (like cdevsw) */
static const struct file_operations mlx5_dbg_fops = {
  .owner          = THIS_MODULE,
  .unlocked_ioctl = mlx5_dbg_ioctl,
};

/* Equivalent of mlx5_init_dbg_dev() */
static int __init mlx5_init_dbg_dev(void)
{
    int err;

    /* Allocate a device number dynamically */
    err = alloc_chrdev_region(&mlx5_dbg_devno, 0, 1, DEVICE_NAME);
    if (err) {
        pr_err("mlx5_dbg: alloc_chrdev_region failed\n");
        return err;
    }

    /* Initialize and add cdev */
    cdev_init(&mlx5_dbg_cdev, &mlx5_dbg_fops);
    mlx5_dbg_cdev.owner = THIS_MODULE;

    err = cdev_add(&mlx5_dbg_cdev, mlx5_dbg_devno, 1);
    if (err) {
        pr_err("mlx5_dbg: cdev_add failed\n");
        unregister_chrdev_region(mlx5_dbg_devno, 1);
        return err;
    }

    /* Create device class for /dev entry */
    mlx5_dbg_class = class_create(DEVICE_NAME);
    if (IS_ERR(mlx5_dbg_class)) {
        pr_err("mlx5_dbg: class_create failed\n");
        cdev_del(&mlx5_dbg_cdev);
        unregister_chrdev_region(mlx5_dbg_devno, 1);
        return PTR_ERR(mlx5_dbg_class);
    }

    /* Create /dev/mlx5dbg */
    if (IS_ERR(device_create(mlx5_dbg_class, NULL, mlx5_dbg_devno, NULL, DEVICE_NAME))) {
        pr_err("mlx5_dbg: device_create failed\n");
        class_destroy(mlx5_dbg_class);
        cdev_del(&mlx5_dbg_cdev);
        unregister_chrdev_region(mlx5_dbg_devno, 1);
        return -ENOMEM;
    }

    pr_info("mlx5_dbg: device /dev/%s created\n", DEVICE_NAME);
    return 0;
}

static void mlx5_dbg_dev_fini(void)
{
    device_destroy(mlx5_dbg_class, mlx5_dbg_devno);
    class_destroy(mlx5_dbg_class);
    cdev_del(&mlx5_dbg_cdev);
    unregister_chrdev_region(mlx5_dbg_devno, 1);

    printk("mlx5_dbg: device /dev/%s removed\n", DEVICE_NAME);
}

#endif

#endif /* VDURA_CHANGES */
