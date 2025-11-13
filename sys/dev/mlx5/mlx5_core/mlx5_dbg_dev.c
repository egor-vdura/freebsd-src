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
#define COPY_FROM_USER(local, user_arg) \
  ({ \
    memcpy(&local, user_arg, sizeof(local)); \
    0; \
  })
#define COPY_TO_USER(user_arg, local) copyout(&(local), user_arg, sizeof(local))
#define COPY_TO_USER_RES(user_arg, local) ({ memcpy(user_arg, &local, sizeof(local)); 0; })
#endif

#ifdef MLX5DBG_LINUX
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
#ifdef MLX5DBG_FREEBSD
static int
mlx5_dbg_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag,
    struct thread *td)
#else
static long mlx5_dbg_ioctl(struct file *file, unsigned int cmd, unsigned long data)
#endif
{
  struct mlx5_core_dev *mdev;
  struct mlx5_get_eq_list eq_list;
  struct mlx5_get_cq_list cq_list;
  struct mlx5_get_eq_info eq_info;
  struct mlx5_get_cq_info cq_info;
  struct mlx5_get_tir_list tir_list;
  struct mlx5_get_tir_info tir_info;
  struct mlx5_tool_addr *devaddr;
  uint32_t *cmd_out;
  int error;

  error = 0;
  switch (cmd) {
  case MLX5_DBG_GET_EQ_LIST:
    printk("MLX5_DBG_GET_EQ_LIST\n");
    error = COPY_FROM_USER(eq_list, data);
    if (error != 0)
      break;
    devaddr = &eq_list.devaddr;
    error = mlx5_dbsf_to_core(devaddr, &mdev);
    if (error != 0)
      break;
    mlx5_dbg_eq_list_fill(mdev, &eq_list);
    error = COPY_TO_USER_RES(data, eq_list);
    break;
  case MLX5_DBG_GET_EQ_INFO:
    printk("MLX5_DBG_GET_EQ_INFO\n");
    error = COPY_FROM_USER(eq_info, data);
    if (error != 0)
      break;
    error = mlx5_dbsf_to_core(&eq_info.devaddr, &mdev);
    if (error != 0)
      break;
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
