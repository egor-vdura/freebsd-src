#include <dev/mlx5/mlx5_en/en.h>
#include <dev/mlx5/mlx5_ib/mlx5_ib.h>

#include <dev/mlx5/fs.h>
#include <dev/mlx5/mlx5_core/fs_core.h>

#include <dev/mlx5/mlx5_en/en.h>
#include <dev/mlx5/mlx5_ifc.h>

#define MAX_FTE_IND 18

struct mlx5i_wqe_eth_pad {
	u8 rsvd0[16];
};

struct mlx5i_tx_wqe {
	struct mlx5_wqe_ctrl_seg     ctrl;
	struct mlx5_wqe_datagram_seg datagram;
	struct mlx5i_wqe_eth_pad      pad;
	struct mlx5_wqe_eth_seg      eth;
	struct mlx5_wqe_data_seg     data[];
};

static
void mlx5i_destroy_tables(struct mlx5_ib_dev* dev);

static
int mlx5i_cmd_fs_create_fte(struct mlx5_core_dev *dev, unsigned int table_id, u32 group_id,
			    unsigned int flow_index, u8 ip_version, u8 ip_protocol, u8 destination_id, bool vxlan)
{
	u32 out[MLX5_ST_SZ_DW(set_fte_out)] = {0};
	u32 *in;
	int err;
	int inlen;
	mlx5_core_dbg(dev, "mlx5i_cmd_fs_create_fte\n");
	if (!dev)
		return -EINVAL;

	inlen = MLX5_ST_SZ_BYTES(set_fte_in) + 1 * MLX5_ST_SZ_BYTES(dest_format_struct);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(set_fte_in, in, opcode,
		 MLX5_CMD_OP_SET_FLOW_TABLE_ENTRY);
	MLX5_SET(set_fte_in, in, op_mod, 0);
	MLX5_SET(set_fte_in, in, other_vport, 0);
	MLX5_SET(set_fte_in, in, vport_number, 0);
	MLX5_SET(set_fte_in, in, table_type, 0);
	MLX5_SET(set_fte_in, in, table_id, table_id);
	MLX5_SET(set_fte_in, in, flow_index, flow_index);

	MLX5_SET(set_fte_in, in, flow_context.group_id, group_id);
	MLX5_SET(set_fte_in, in, flow_context.flow_tag, 0);
	MLX5_SET(set_fte_in, in, flow_context.action, MLX5_FLOW_CONTEXT_ACTION_FWD_DEST);
	MLX5_SET(set_fte_in, in, flow_context.destination_list_size, 1);
	MLX5_SET(set_fte_in, in, flow_context.flow_counter_list_size, 0);
	MLX5_SET(set_fte_in, in, flow_context.packet_reformat_id, 0);
	MLX5_SET(set_fte_in, in, flow_context.modify_header_id, 0);
	if (vxlan) {
		if (ip_version != 0)
			MLX5_SET(set_fte_in, in, flow_context.match_value.inner_headers.ip_version, ip_version);
		if (ip_protocol != 0)
			MLX5_SET(set_fte_in, in, flow_context.match_value.inner_headers.ip_protocol, ip_protocol);
	}else{
		if (ip_version != 0)
			MLX5_SET(set_fte_in, in, flow_context.match_value.outer_headers.ip_version, ip_version);
		if (ip_protocol != 0)
			MLX5_SET(set_fte_in, in, flow_context.match_value.outer_headers.ip_protocol, ip_protocol);
	}

	MLX5_SET(set_fte_in, in, flow_context.destination[0].dest_format_struct.destination_type, MLX5_FLOW_DESTINATION_TYPE_TIR);
	MLX5_SET(set_fte_in, in, flow_context.destination[0].dest_format_struct.destination_id, destination_id);
	MLX5_SET(set_fte_in, in, flow_context.destination[0].dest_format_struct.destination_table_type, 0);

	err = mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));
	if (err)
		printf("FTE with flow_index %u creation failure\n", flow_index);

	kfree(in);
	return err;
}

static
int mlx5i_fs_destroy_fte(struct mlx5_ib_dev* dev, unsigned int table_id, unsigned int index)
{
	u32 in[MLX5_ST_SZ_DW(delete_fte_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(delete_fte_out)] = {0};
	int err;

	MLX5_SET(delete_fte_in, in, opcode, MLX5_CMD_OP_DELETE_FLOW_TABLE_ENTRY);
	MLX5_SET(delete_fte_in, in, table_type, 0);
	MLX5_SET(delete_fte_in, in, table_id, table_id);
	MLX5_SET(delete_fte_in, in, flow_index, index);

	err =  mlx5_cmd_exec(dev->mdev, in, sizeof(in), out, sizeof(out));
	if (err)
		mlx5_ib_err(dev, "mlx5i_fs_destroy_fte failure\n");

	return err;
}

static
int mlx5i_cmd_fs_create_fg(struct mlx5_core_dev *dev,
	unsigned int table_id, bool inner_vxlan,
			  u32 start_flow_index, u32 end_flow_index, bool match_criteria_enable,
			  bool match_ip_protocol, bool match_ip_version, unsigned int *group_id)
{
	u32 in[MLX5_ST_SZ_DW(create_flow_group_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(create_flow_group_out)] = {0};
	int err;
	int inlen = MLX5_ST_SZ_BYTES(create_flow_group_in);
	if (!dev)
		return -EINVAL;

	mlx5_core_dbg(dev, "mlx5i_cmd_fs_create_fg\n");
	MLX5_SET(create_flow_group_in, in, opcode,
		 MLX5_CMD_OP_CREATE_FLOW_GROUP);
	MLX5_SET(create_flow_group_in, in, table_type, 0);
	MLX5_SET(create_flow_group_in, in, table_id, table_id);
	MLX5_SET(create_flow_group_in, in, vport_number, 0);
	MLX5_SET(create_flow_group_in, in, other_vport, 0);
	MLX5_SET(create_flow_group_in, in, start_flow_index, start_flow_index);
	MLX5_SET(create_flow_group_in, in, end_flow_index, end_flow_index);
	if (match_criteria_enable) {
		if (inner_vxlan) {
			MLX5_SET(create_flow_group_in, in, match_criteria_enable, MLX5_MATCH_INNER_HEADERS);
			if (match_ip_protocol)
				MLX5_SET_TO_ONES(create_flow_group_in, in, match_criteria.inner_headers.ip_protocol);
			if (match_ip_version)
				MLX5_SET_TO_ONES(create_flow_group_in, in, match_criteria.inner_headers.ip_version);
		} else {
			MLX5_SET(create_flow_group_in, in, match_criteria_enable, MLX5_MATCH_OUTER_HEADERS);
			if (match_ip_protocol)
				MLX5_SET_TO_ONES(create_flow_group_in, in, match_criteria.outer_headers.ip_protocol);
			if (match_ip_version)
				MLX5_SET_TO_ONES(create_flow_group_in, in, match_criteria.outer_headers.ip_version);
		}


	}


	err = mlx5_cmd_exec(dev, in, inlen, out, sizeof(out));
	if (!err)
		*group_id = MLX5_GET(create_flow_group_out, out, group_id);

	return err;
}


static
void mlx5i_fs_destroy_fg(struct mlx5_ib_dev* dev, unsigned int group_id, unsigned int table_id)
{
	int err;
	u32 in[MLX5_ST_SZ_DW(destroy_flow_group_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(destroy_flow_group_out)] = {0};

	MLX5_SET(destroy_flow_group_in, in, opcode,
		 MLX5_CMD_OP_DESTROY_FLOW_GROUP);
	MLX5_SET(destroy_flow_group_in, in, table_type, 0);
	MLX5_SET(destroy_flow_group_in, in, table_id,   table_id);
	MLX5_SET(destroy_flow_group_in, in, group_id, group_id);

	err = mlx5_cmd_exec(dev->mdev, in, sizeof(in), out, sizeof(out));
	if(err)
		mlx5_ib_err(dev, "mlx5i_fs_destroy_fg failure\n");
}

static
int mlx5i_cmd_fs_create_ft(struct mlx5_core_dev *dev,
			  u16 vport, enum fs_ft_type type, unsigned int level,
			  unsigned int log_size, const char *name, unsigned int *table_id, unsigned int *next_id)
{
	int en_encap = 0;
	int en_decap = 0;
	int term = 0;

	u32 in[MLX5_ST_SZ_DW(create_flow_table_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(create_flow_table_out)] = {0};
	int err;

	mlx5_core_dbg(dev, "mlx5i_cmd_fs_create_ft\n");
	if (!dev)
		return -EINVAL;

	MLX5_SET(create_flow_table_in, in, opcode,
		 MLX5_CMD_OP_CREATE_FLOW_TABLE);

	MLX5_SET(create_flow_table_in, in, uid, 0x0);
	MLX5_SET(create_flow_table_in, in, table_type, type);
	MLX5_SET(create_flow_table_in, in, flow_table_context.level, level);
	MLX5_SET(create_flow_table_in, in, flow_table_context.log_size, log_size);

	MLX5_SET(create_flow_table_in, in, vport_number, 0);
	MLX5_SET(create_flow_table_in, in, other_vport, 0);

	MLX5_SET(create_flow_table_in, in, flow_table_context.decap_en,
		 en_decap);
	MLX5_SET(create_flow_table_in, in, flow_table_context.reformat_en,
		 en_encap);
	MLX5_SET(create_flow_table_in, in, flow_table_context.termination_table,
		 term);

	if (next_id == NULL)
	{
		MLX5_SET(create_flow_table_in, in,
			 flow_table_context.table_miss_action,
			 MLX5_FLOW_TABLE_MISS_ACTION_DEF);
	}
	else
	{
		MLX5_SET(create_flow_table_in, in,
			 flow_table_context.table_miss_action,
			 MLX5_FLOW_TABLE_MISS_ACTION_FWD);
			 MLX5_SET(create_flow_table_in, in,
				  flow_table_context.table_miss_id, *next_id);
	}

	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (!err)
		*table_id = MLX5_GET(create_flow_table_out, out, table_id);

	return err;
}


static
void mlx5i_fs_destroy(struct mlx5_ib_dev* dev, unsigned int table_id)
{
	int err;
	u32 in[MLX5_ST_SZ_DW(destroy_flow_table_in)] = {0};
	u32 out[MLX5_ST_SZ_DW(destroy_flow_table_out)] = {0};
	MLX5_SET(destroy_flow_table_in, in, opcode, MLX5_CMD_OP_DESTROY_FLOW_TABLE);
	MLX5_SET(destroy_flow_table_in, in, table_type, 0);
	MLX5_SET(destroy_flow_table_in, in, table_id, table_id);
	err = mlx5_cmd_exec(dev->mdev, in, sizeof(in), out, sizeof(out));
	if(err)
		mlx5_ib_err(dev, "mlx5i_fs_destroy failure\n");
}

static
u32 get_tir_number(int i, struct mlx5e_priv *epriv)
{
	return (i % 2 ? epriv->tirn_inner_vxlan[i/2] : epriv->tirn[i/2]);
	//return epriv->tirn[0];
}

static
int mlx5i_create_fs(struct mlx5_ib_dev *dev, struct mlx5e_priv *epriv)
{
	int err = 0;
	struct mlx5_core_dev *mdev = dev->mdev;
	unsigned int table_id;
	unsigned int flow_index = 0;
	/* setup root flow table with the default rule*/
	err |= mlx5i_cmd_fs_create_ft(mdev,
		0, 0, 67, 0, "roottable0", &(mdev->table_ids[0]), NULL);

	err |= mlx5i_cmd_fs_create_ft(mdev,
		0, 0, 58, 0x7, "roottable1", &table_id, &(mdev->table_ids[0]));
	mdev->table_ids[1] = table_id;

	/* vxlan */
	err |= mlx5i_cmd_fs_create_fg(mdev, table_id, true, 0,  7, true,  true, true, &(mdev->group_ids[0]));
	/* non vxlan */
	err |= mlx5i_cmd_fs_create_fg(mdev, table_id, false, 8,  15, true,  true, true, &(mdev->group_ids[1]));
	err |= mlx5i_cmd_fs_create_fg(mdev, table_id, false, 16, 17, true, false, true, &(mdev->group_ids[2]));
	err |= mlx5i_cmd_fs_create_fg(mdev, table_id, false, 18, MAX_FTE_IND, false, false, false, &(mdev->group_ids[3]));

	u8 protocols[] = {
		IPPROTO_TCP,
		IPPROTO_UDP,
		IPPROTO_AH,
		IPPROTO_ESP
	};
	u8 versions[] = {4, 6};
	for (flow_index = 0; flow_index <= 15; flow_index++)
	{
		/* First FG  (first 8 FTEs) are VXLAN
		 * Second FG (last 8 FTEs) are non VXLAN */
		bool vxlan = (flow_index <= 7);


		unsigned int tir_idx;
		unsigned int tir_n;
		if (vxlan) /* Vxlan: Even TIRs are VXLAN. */
		{
			tir_idx = flow_index * 2;
			tir_n = epriv->tirn_inner_vxlan[tir_idx];
		}
		else /* uneven TIRs are not vxlan: Second FG (last 8 FTEs) are non VXLAN */
		{
			tir_idx = (flow_index - 8) * 2 + 1;
			tir_n = epriv->tirn[tir_idx];
		}

		/* Protocols are linearly aligned with the TIRs, and repeat 4 times (vxlan and non vxlan, v4 and v6) */
		u8 protocol = protocols[tir_idx / 4];
		/* The versions are linearly aligned with the TIRS, and repeat 2 times (vxlan and non vxlan) */
		u8 version = versions[tir_idx % 2];

        	err |= mlx5i_cmd_fs_create_fte(mdev, table_id, vxlan ? mdev->group_ids[0] : mdev->group_ids[1], flow_index, version, protocol, tir_n, vxlan);
	}
        err |= mlx5i_cmd_fs_create_fte(mdev, table_id, mdev->group_ids[2], flow_index, 4, 0, get_tir_number(flow_index, epriv), false);
	flow_index++;
        err |= mlx5i_cmd_fs_create_fte(mdev, table_id, mdev->group_ids[2], flow_index, 6, 0, get_tir_number(flow_index, epriv), false);
	flow_index++;

        err |= mlx5i_cmd_fs_create_fte(mdev, table_id, mdev->group_ids[3], flow_index, 0, 0, get_tir_number(flow_index, epriv), false);

	// TODO Integrate FT creation with the pre-existing infra. For now, just do basic error handling
	if (err != 0)
		mlx5i_destroy_tables(dev);

	return err;
}

static
int mlx5i_activate_fs(struct mlx5_core_dev *mdev)
{
	/* Set our underlay QP as the root of the FT */
	return mlx5_cmd_update_root_ft(mdev, FS_FT_NIC_RX, mdev->table_ids[1]);
}

static
void mlx5i_destroy_tables(struct mlx5_ib_dev* dev)
{
	for (int flow_index = 0; flow_index < MAX_FTE_IND + 1; flow_index++) {
		mlx5i_fs_destroy_fte(dev, dev->mdev->table_ids[1], flow_index);
	}

	mlx5i_fs_destroy_fg(dev, dev->mdev->group_ids[0], dev->mdev->table_ids[1]);
	mlx5i_fs_destroy_fg(dev, dev->mdev->group_ids[1], dev->mdev->table_ids[1]);
	mlx5i_fs_destroy_fg(dev, dev->mdev->group_ids[2], dev->mdev->table_ids[1]);
}



static
int mlx5_ib_alloc_en_priv(struct mlx5_ib_dev *dev, if_t ipoib_if)
{
	int err;
	struct mlx5_core_dev *mdev = dev->mdev;
	int ncv = mdev->priv.eq_table.num_comp_vectors;

	struct mlx5e_priv* priv = malloc_domainset(sizeof(*priv) +
	    (sizeof(priv->channel[0]) * mdev->priv.eq_table.num_comp_vectors),
	    M_MLX5EN, mlx5_dev_domainset(mdev), M_WAITOK | M_ZERO);

	/* Needed because of m_snd_tag_init */
	priv->ifp = ipoib_if;
	dev->priv = priv;

	/* setup all static fields and internal structures */
	mlx5_core_dbg(mdev, "mlx5e_priv_static_init (%d)\n", mdev->priv.eq_table.num_comp_vectors);
	if (mlx5e_priv_static_init(priv, mdev, mdev->priv.eq_table.num_comp_vectors)) {
		mlx5_core_err(mdev, "mlx5e_priv_static_init() failed\n");
		goto err_dealloc_priv;
	}

	/* Populate mlx5e metadata */
	err = mlx5e_build_ifp_priv(mdev, priv, ncv);
	if (err) {
		mlx5_core_err(mdev, "mlx5e_build_ifp_priv() failed %d\n", err);
		goto err_dealloc_priv;
	}

	priv->wq = mdev->priv.health.wq_watchdog;

	err = mlx5_core_alloc_pd(mdev, &priv->pdn, 0);
	if (err) {
		mlx5_core_err(mdev, "mlx5_core_alloc_pd() failed %d\n", err);
		goto err_free_wq;
	}
	err = mlx5_alloc_transport_domain(mdev, &priv->tdn, 0);
	if (err) {
		mlx5_core_err(mdev, "mlx5_alloc_transport_domain() failed %d\n", err);
		goto err_dealloc_pd;
	}
	err = mlx5e_create_mkey(priv, priv->pdn, &priv->mr);
	if (err) {
		mlx5_core_err(mdev, "mlx5e_create_mkey() failed %d\n", err);
		goto err_dealloc_transport_domain;
	}

	err = mlx5e_open_drop_rq(priv, &priv->drop_rq);
	if (err) {
		mlx5_core_err(mdev, "mlx5e_open_drop_rq() failed %d\n", err);
		goto err_create_mkey;
	}
	err = mlx5e_open_rqts(priv);
	if (err) {
		mlx5_core_err(mdev, "mlx5e_open_rqts() failed %d\n", err);
		goto err_open_drop_rq;
	}

	err = mlx5e_open_tirs(priv);
	if (err) {
		mlx5_core_err(mdev, "mlx5e_open_tirs() failed %d\n", err);
		goto err_open_rqts;
	}

	mlx5_core_dbg(mdev, "mlx5_ib_setup_en_priv success!!\n");
	return 0;

err_open_rqts:
	mlx5e_close_rqts(priv);

err_open_drop_rq:
	mlx5e_close_drop_rq(&priv->drop_rq);
err_create_mkey:
	mlx5_core_destroy_mkey(priv->mdev, &priv->mr);

err_dealloc_transport_domain:
	mlx5_dealloc_transport_domain(mdev, priv->tdn, 0);

err_dealloc_pd:
	mlx5_core_dealloc_pd(mdev, priv->pdn, 0);

err_free_wq:
	flush_workqueue(priv->wq);

err_dealloc_priv:
	mlx5e_priv_static_destroy(priv, mdev, mdev->priv.eq_table.num_comp_vectors);
	free(priv, M_MLX5EN);
	return 1;
}

static
void mlx5_ib_free_en_priv(struct mlx5e_priv* priv)
{
	mlx5e_close_tirs(priv);
	mlx5e_close_rqts(priv);
	mlx5e_close_drop_rq(&priv->drop_rq);
	mlx5_core_destroy_mkey(priv->mdev, &priv->mr);
	mlx5_dealloc_transport_domain(priv->mdev, priv->tdn, 0);
	mlx5_core_dealloc_pd(priv->mdev, priv->pdn, 0);
	flush_workqueue(priv->wq);

	mlx5e_priv_static_destroy(priv, priv->mdev, priv->mdev->priv.eq_table.num_comp_vectors);

	free(priv, M_MLX5EN);
}

int mlx5_ib_direct_init(struct ib_device *ca, if_t direct_if, u32 qpn, u16 pkey_index)
{
	struct mlx5_ib_dev *dev = container_of(ca, struct mlx5_ib_dev, ib_dev);
	struct mlx5e_priv *epriv;
	int err = 0;
	dev->qpn = qpn;
	dev->pkey_index = pkey_index;

	err = mlx5_ib_alloc_en_priv(dev, direct_if);
	if (err) {
		mlx5_ib_err(dev, "mlx5_ib_setup_en_priv failure %d\n", err);
		return err;
	}

	epriv   = dev->priv;
	PRIV_LOCK(epriv);

	dev->mdev->vport = 0;
	dev->mdev->e_ipoib_en = true;
	dev->mdev->underlay_qpn = qpn;

	err = mlx5i_create_fs(dev, epriv);
	if (err) {
		PRIV_UNLOCK(epriv);
		mlx5_ib_free_en_priv(epriv);
		dev->priv = NULL;
		mlx5_ib_err(dev, "mlx5i_create_fs failed, %d\n", err);
		return err;
	}

	PRIV_UNLOCK(epriv);
	mlx5_ib_dbg(dev, "mlx5_ib_direct_init\n");
	return 0;
}

int mlx5_ib_direct_open(struct ib_device *ca)
{
	struct mlx5_ib_dev *dev = container_of(ca, struct mlx5_ib_dev, ib_dev);
	struct mlx5e_priv *epriv = dev->priv;
	int err = 0;

	PRIV_LOCK(epriv);
	err = mlx5e_open_tises(epriv);
	if (err) {
		mlx5_ib_err(dev, "mlx5e_open_tises failed, %d\n", err);
		goto err_remove_fs_underlay_qp;
	}

	err = mlx5e_open_channels(epriv);
	if (err)
	{
		mlx5_ib_err(dev, "mlx5e_open_channels failed %d\n", err);
		goto err_close_tises;
	}

	err = mlx5e_activate_rqt(epriv);
	if (err) {
		mlx5_ib_err(dev, "mlx5e_activate_rqt failed %d\n", err);
		goto err_close_channels;
	}

	err = mlx5i_activate_fs(dev->mdev);
	if (err) {
		mlx5_ib_err(dev, "mlx5i_activate_fs failed %d\n", err);
		goto err_deactivate_rqt;
	}

	set_bit(MLX5E_STATE_OPENED, &epriv->state);
	PRIV_UNLOCK(epriv);
	mlx5_ib_dbg(dev, "mlx5_ib_direct_open\n");
	return 0;

err_deactivate_rqt:
	mlx5e_deactivate_rqt(epriv);
err_close_channels:
	mlx5e_close_channels(epriv);
err_close_tises:
	mlx5e_close_tises(epriv);
err_remove_fs_underlay_qp:
	mlx5i_destroy_tables(dev);
	mlx5_ib_err(dev, "mlx5_ib_direct_open failure!\n");

	PRIV_UNLOCK(epriv);
	return err;
}

void mlx5_ib_direct_close(struct ib_device *ca)
{
	struct mlx5_ib_dev *dev = container_of(ca, struct mlx5_ib_dev, ib_dev);
	struct mlx5e_priv *epriv = dev->priv;

	mlx5_ib_dbg(dev, "mlx5_ib_direct_close\n");
	if (test_bit(MLX5E_STATE_OPENED, &epriv->state) == 0)
		return;

	PRIV_LOCK(epriv);
	clear_bit(MLX5E_STATE_OPENED, &epriv->state);
	mlx5e_deactivate_rqt(epriv);
	mlx5e_close_channels(epriv);
	mlx5e_close_tises(epriv);
	PRIV_UNLOCK(epriv);
}

void mlx5_ib_direct_teardown(struct ib_device *ca)
{
	struct mlx5_ib_dev *dev = container_of(ca, struct mlx5_ib_dev, ib_dev);
	mlx5_ib_dbg(dev, "mlx5_ib_direct_teardown\n");
	mlx5i_destroy_tables(dev);

	mlx5i_fs_destroy(dev, dev->mdev->table_ids[1]);
	mlx5i_fs_destroy(dev, dev->mdev->table_ids[0]);
	mlx5_ib_free_en_priv(dev->priv);
}


enum {
	MLX5E_TC_PRIO = 0,
	MLX5E_PROMISC_PRIO,
	MLX5E_NIC_PRIO,
};

static int
mlx5i_sq_xmit(struct mlx5e_sq *sq, struct mlx5_av *av, struct mbuf **mbp)
{
	bus_dma_segment_t segs[MLX5E_MAX_TX_MBUF_FRAGS];
	struct mlx5e_xmit_args args = {};
	struct mlx5_wqe_data_seg *dseg;
	struct mlx5i_tx_wqe *wqe;
	int nsegs;
	int err;
	int x;
	struct mbuf *mb;
	u16 ds_cnt;
	u16 pi;
	u8 opcode;

#ifdef KERN_TLS
top:
#endif
	/* Return ENOBUFS if the queue is full */
	if (unlikely(!mlx5e_sq_has_room_for(sq, 2 * MLX5_SEND_WQE_MAX_WQEBBS))) {
		sq->stats.enobuf++;
		return (ENOBUFS);
	}

	/* Align SQ edge with NOPs to avoid WQE wrap around */
	pi = ((~sq->pc) & sq->wq.sz_m1);
	if (pi < (MLX5_SEND_WQE_MAX_WQEBBS - 1)) {
		/* Send one multi NOP message instead of many */
		mlx5e_send_nop(sq, (pi + 1) * MLX5_SEND_WQEBB_NUM_DS);
		pi = ((~sq->pc) & sq->wq.sz_m1);
		if (pi < (MLX5_SEND_WQE_MAX_WQEBBS - 1)) {
			sq->stats.enobuf++;
			return (ENOMEM);
		}
	}

#ifdef KERN_TLS
	/* Special handling for TLS packets, if any */
	switch (mlx5e_sq_tls_xmit(sq, &args, mbp)) {
	case MLX5E_TLS_LOOP:
		goto top;
	case MLX5E_TLS_FAILURE:
		mb = *mbp;
		err = ENOMEM;
		goto tx_drop;
	case MLX5E_TLS_DEFERRED:
		return (0);
	case MLX5E_TLS_CONTINUE:
	default:
		break;
	}
#endif

	/* Setup local variables */
	pi = sq->pc & sq->wq.sz_m1;
	wqe = mlx5_wq_cyc_get_wqe(&sq->wq, pi);

	memset(wqe, 0, sizeof(*wqe));

	/* get pointer to mbuf */
	mb = *mbp;

	if (mb->m_pkthdr.csum_flags & (CSUM_IP | CSUM_TSO)) {
		wqe->eth.cs_flags |= MLX5_ETH_WQE_L3_CSUM;
	}
	if (mb->m_pkthdr.csum_flags & (CSUM_TCP | CSUM_UDP | CSUM_UDP_IPV6 | CSUM_TCP_IPV6 | CSUM_TSO)) {
		wqe->eth.cs_flags |= MLX5_ETH_WQE_L4_CSUM;
	}
	if (wqe->eth.cs_flags == 0) {
		sq->stats.csum_offload_none++;
	}
	if (mb->m_pkthdr.csum_flags & CSUM_TSO) {
		u32 payload_len;
		u32 mss = mb->m_pkthdr.tso_segsz;
		u32 num_pkts;

		wqe->eth.mss = cpu_to_be16(mss);
		opcode = MLX5_OPCODE_LSO;
		if (args.ihs == 0)
		{
			args.ihs = mlx5e_get_full_header_size(mb, NULL, sq->priv->mdev->e_ipoib_en);
		}
		if (unlikely(args.ihs == 0)) {
			err = EINVAL;
			goto tx_drop;
		}
		payload_len = mb->m_pkthdr.len - args.ihs;
		if (payload_len == 0)
			num_pkts = 1;
		else
			num_pkts = DIV_ROUND_UP(payload_len, mss);
		sq->mbuf[pi].num_bytes = payload_len + (num_pkts * args.ihs);


		sq->stats.tso_packets++;
		sq->stats.tso_bytes += payload_len;
	} else {
		opcode = MLX5_OPCODE_SEND;
		sq->mbuf[pi].num_bytes = max_t (unsigned int,
		    mb->m_pkthdr.len, ETHER_MIN_LEN - ETHER_CRC_LEN);
	}

	memcpy(&wqe->datagram.av, av, sizeof(*av));

	if (likely(args.ihs == 0)) {
		/* nothing to inline */
	} else {
		/* check if inline header size is too big */
		if (unlikely(args.ihs > sq->max_inline)) {
			if (unlikely(mb->m_pkthdr.csum_flags & (CSUM_TSO |
			   CSUM_ENCAP_VXLAN))) {
				err = EINVAL;
				goto tx_drop;
			}
			args.ihs = sq->max_inline;
		}
		m_copydata(mb, 0, args.ihs, wqe->eth.inline_hdr_start);
		m_adj(mb, args.ihs);
		wqe->eth.inline_hdr_sz = cpu_to_be16(args.ihs);
	}

	ds_cnt = sizeof(*wqe) / MLX5_SEND_WQE_DS;
	if (args.ihs > sizeof(wqe->eth.inline_hdr_start)) {
		ds_cnt += DIV_ROUND_UP(args.ihs - sizeof(wqe->eth.inline_hdr_start),
		    MLX5_SEND_WQE_DS);
	}
	dseg = ((struct mlx5_wqe_data_seg *)&wqe->ctrl) + ds_cnt;

	err = bus_dmamap_load_mbuf_sg(sq->dma_tag, sq->mbuf[pi].dma_map,
	    mb, segs, &nsegs, BUS_DMA_NOWAIT);
	if (err == EFBIG) {
		/* Update statistics */
		sq->stats.defragged++;
		/* Too many mbuf fragments */
		mb = m_defrag(*mbp, M_NOWAIT);
		if (mb == NULL) {
			mb = *mbp;
			goto tx_drop;
		}
		/* Try again */
		err = bus_dmamap_load_mbuf_sg(sq->dma_tag, sq->mbuf[pi].dma_map,
		    mb, segs, &nsegs, BUS_DMA_NOWAIT);
	}
	/* Catch errors */
	if (err != 0)
	{
		goto tx_drop;
	}

	/* Make sure all mbuf data, if any, is visible to the bus */
	if (nsegs != 0) {
		bus_dmamap_sync(sq->dma_tag, sq->mbuf[pi].dma_map,
		    BUS_DMASYNC_PREWRITE);
	} else {
		/* All data was inlined, free the mbuf. */
		bus_dmamap_unload(sq->dma_tag, sq->mbuf[pi].dma_map);
		m_freem(mb);
		mb = NULL;
	}
	for (x = 0; x != nsegs; x++) {
		if (segs[x].ds_len == 0)
			continue;
		dseg->addr = cpu_to_be64((uint64_t)segs[x].ds_addr);
		dseg->lkey = sq->mkey_be;
		dseg->byte_count = cpu_to_be32((uint32_t)segs[x].ds_len);
		dseg++;
	}

	ds_cnt = (dseg - ((struct mlx5_wqe_data_seg *)&wqe->ctrl));

	wqe->ctrl.opmod_idx_opcode = cpu_to_be32((sq->pc << 8) | opcode);
	wqe->ctrl.qpn_ds = cpu_to_be32((sq->sqn << 8) | ds_cnt);
	wqe->ctrl.imm = cpu_to_be32(args.tisn << 8);

	if (mlx5e_do_send_cqe_inline(sq))
	/* TODO: Linux sets 0 here? */
		wqe->ctrl.fm_ce_se = MLX5_WQE_CTRL_CQ_UPDATE;
	else
		wqe->ctrl.fm_ce_se = 0;

	/* Copy data for doorbell */
	memcpy(sq->doorbell.d32, &wqe->ctrl, sizeof(sq->doorbell.d32));

	/* Store pointer to mbuf */
	sq->mbuf[pi].mbuf = mb;
	sq->mbuf[pi].num_wqebbs = DIV_ROUND_UP(ds_cnt, MLX5_SEND_WQEBB_NUM_DS);
	if (unlikely(args.mst != NULL))
		sq->mbuf[pi].mst = m_snd_tag_ref(args.mst);
	else
		MPASS(sq->mbuf[pi].mst == NULL);

	sq->pc += sq->mbuf[pi].num_wqebbs;

	/* Count all traffic going out */
	sq->stats.packets++;
	sq->stats.bytes += sq->mbuf[pi].num_bytes;

	*mbp = NULL;    /* safety clear */
	return (0);

tx_drop:
	sq->stats.dropped++;
	*mbp = NULL;
	m_freem(mb);
	return err;
}


static
int mlx5i_xmit_locked(struct mbuf *mb, struct mlx5_av *av, struct mlx5e_sq *sq)
{
	int err = 0;

	if (unlikely((if_getdrvflags(sq->ifp) & IFF_DRV_RUNNING) == 0 ||
	    READ_ONCE(sq->running) == 0)) {
		printk(KERN_WARNING "Driver not running!\n");
		m_freem(mb);
		return (ENETDOWN);
	}

	/* Do transmit */
	if (mlx5i_sq_xmit(sq, av, &mb) != 0) {
		/* NOTE: m_freem() is NULL safe */
		m_freem(mb);
		err = ENOBUFS;
	}

	/* Write the doorbell record, if any. */
	mlx5e_tx_notify_hw(sq, false);

	/*
	 * Check if we need to start the event timer which flushes the
	 * transmit ring on timeout:
	 */
	if (unlikely(sq->cev_next_state == MLX5E_CEV_STATE_INITIAL &&
	    sq->cev_factor != 1)) {
		/* start the timer */
		mlx5e_sq_cev_timeout(sq);
	} else {
		/* don't send NOPs yet */
		sq->cev_next_state = MLX5E_CEV_STATE_HOLD_NOPS;
	}
	return (err);
}

static
void ah2av(struct ib_ah *ah, struct mlx5_av *av)
{
	struct ib_ah_attr ah_attr = {0};
	int err;

	err = ah->device->query_ah(ah, &ah_attr);
	if (!err) {
		av->rlid = cpu_to_be16(ah_attr.dlid);
		av->stat_rate_sl = ah_attr.static_rate << 4;
	} else {
		printf("ERROR: ah2av: err %d\n", err);
	}
}

void mlx5i_xmit(struct ib_ah* ah, u32 dqpn, u32 dqkey, struct mbuf *mb)
{
	struct mlx5_ib_ah *mah = to_mah(ah);
	struct mlx5_av* av = &(mah->av);

	struct mlx5e_sq *sq;
	struct mlx5_ib_dev* ib_dev = container_of(ah->device, struct mlx5_ib_dev, ib_dev);
	struct mlx5e_priv *priv = ib_dev->priv;

	av->key.qkey.qkey = cpu_to_be32(dqkey);
	/* ext bit (31st bit) should be set for IPoIB */
	av->dqp_dct = cpu_to_be32(dqpn | (1u << 31));
	av->fl_mlid = 0;
	av->grh_gid_fl = cpu_to_be32(1u << 30);
	ah2av(ah, av);

	if (mb->m_pkthdr.csum_flags & CSUM_SND_TAG) {
		MPASS(mb->m_pkthdr.snd_tag->ifp == priv->ifp);
		sq = mlx5e_select_queue_by_send_tag(priv->ifp, mb);
		if (unlikely(sq == NULL)) {
			goto select_queue;
		}
	} else {
select_queue:
		sq = mlx5e_select_queue(priv, mb);
		if (unlikely(sq == NULL)) {
			mlx5_ib_err(ib_dev, "mlx5i_xmit Invalid send queue\n");
			/* Free mbuf */
			m_freem(mb);

			/* Invalid send queue */
			return;
		}
	}

	mtx_lock(&sq->lock);

	mlx5i_xmit_locked(mb, av, sq);

	mtx_unlock(&sq->lock);
}

