/* 
	[8226][MDSS] ASUS MDSS DEBUG UTILITY (AMDU) support.

	This file is built only
		#ifdef CONFIG_ASUS_MDSS_DEBUG_UTILITY

*/

#include "mdss_asus_debug.h"
#include <linux/debugfs.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/printk.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/power_supply.h>

// include files for QCT
#include "mdss_fb.h"
#include "mdss_dsi.h"

static bool pixel_invert = false;
static bool stick_sol1_on = false;
extern int pm8226_get_prop_batt_status(void);

////////////////////////////////////////////////////////////////////////////////
// Global data used for ASUS MOBILE DISPLAY UTILITY

struct watch_dog_data{
	struct notifier_block fb_notifier;
	struct delayed_work watch_dog;
	int watch_dog_interval;
	int dim_time_out;
};
struct panel_status{
	bool	ambient;
	bool	power_mode;
	bool	auto_dim;
	int		last_time_update;
};
struct fb_status{
	struct msm_fb_data_type *mfd;
	struct fb_info *fb_info;
	bool	blank;
	int		fps;
};

// We would like to use static variable for it's stable then kalloc
struct amdu_global_data
{
	struct dentry *ent_reg;
	unsigned int debug_log_flag;
	struct mdss_dsi_ctrl_pdata *ctrl;

	// components info
	struct panel_status panel_status;
	struct fb_status fb_status;
	struct watch_dog_data watch_dog_data;

	// string variable last
	char	debug_str[128];
};

static struct amdu_global_data amdu_data;



////////////////////////////////////////////////////////////////////////////////
// DEBUGFS

static int check_panel_status(void);

static int mdss_debug_cmd_open(struct inode *inode, struct file *file)
{
	return 0;
}

static int mdss_debug_cmd_release(struct inode *inode, struct file *file)
{
	return 0;
}



int _send_mipi_write_cmd(struct dsi_cmd_desc* test_cmd);
int send_mipi_write_cmd(char cmd0, char cmd1);
int send_mipi_read_cmd(char cmd0, char cmd1);

#define CMD_BUF_SIZE 256
static char cmd_buf[CMD_BUF_SIZE];
static ssize_t mdss_debug_base_cmd_write(struct file *file,
		    const char __user *user_buf, size_t count, loff_t *ppos)
{
	if (count >= sizeof(cmd_buf))
		return -EFAULT;

	memset(cmd_buf,0,CMD_BUF_SIZE);
	if (copy_from_user(cmd_buf, user_buf, count))
		return -EFAULT;

	printk("MDSS:[mdss_debug.c]:%s:+++, \"%s\",count=%d\n", __func__,cmd_buf,count);


	cmd_buf[count] = 0;	/* end of string */

	// Build Commands
	if (!strncmp(STICKING_SOL1,cmd_buf,strlen(STICKING_SOL1))){
		stick_sol1_on = (!stick_sol1_on);
		printk("stick_sol1_on:%d\n",stick_sol1_on ? 1:0);
	}
	if (!strncmp(DSI_CMD_NORON,cmd_buf,strlen(DSI_CMD_NORON))){

		// 13 Normal Display mode on
		printk("echo > 'write:05 01 00 00 00 00 01 13' > /d/mdp/amdu_cmd\n");
		strcpy(cmd_buf,"write:05 01 00 00 00 00 01 13");

	}
	else if (!strncmp(DSI_CMD_ALLPON,cmd_buf,strlen(DSI_CMD_ALLPON))){

		// 23 all pixel on
		printk("echo 'write:05 01 00 00 00 00 01 23' > /d/mdp/amdu_cmd\n");
		strcpy(cmd_buf,"write:05 01 00 00 00 00 01 23");
	}
	else if (!strncmp(DSI_CMD_SLPIN,cmd_buf,strlen(DSI_CMD_SLPIN))){

		// 10 sleep in
		printk("echo 'write:05 01 00 00 00 00 01 10' > /d/mdp/amdu_cmd\n");
		strcpy(cmd_buf,"write:05 01 00 00 00 00 01 10");
	}
	else if (!strncmp(DSI_CMD_SLPOUT,cmd_buf,strlen(DSI_CMD_SLPOUT))){

		// 11 sleep out
		printk("echo 'write:05 01 00 00 00 00 01 11' > /d/mdp/amdu_cmd\n");
		strcpy(cmd_buf,"write:05 01 00 00 00 00 01 11");
	}
	else if (!strncmp(DSI_CMD_DISPOFF,cmd_buf,strlen(DSI_CMD_DISPOFF))){

		// 28 display off
		printk("echo 'write:05 01 00 00 00 00 01 28' > /d/mdp/amdu_cmd\n");
		strcpy(cmd_buf,"write:05 01 00 00 00 00 01 28");
	}
	else if (!strncmp(DSI_CMD_DISPON,cmd_buf,strlen(DSI_CMD_DISPON))){

		// 29 display on
		printk("echo 'write:05 01 00 00 00 00 01 29' > /d/mdp/amdu_cmd\n");
		strcpy(cmd_buf,"write:05 01 00 00 00 00 01 29");
	}
	else if (!strncmp(DSI_CMD_INVOFF,cmd_buf,strlen(DSI_CMD_INVOFF))){

		// 20 invert off
		printk("echo 'write:05 01 00 00 00 00 01 20' > /d/mdp/amdu_cmd\n");
		strcpy(cmd_buf,"write:05 01 00 00 00 00 01 20");
	}
	else if (!strncmp(DSI_CMD_INVON,cmd_buf,strlen(DSI_CMD_INVON))){

		// 21 invert on
		printk("echo 'write:05 01 00 00 00 00 01 21' > /d/mdp/amdu_cmd\n");
		strcpy(cmd_buf,"write:05 01 00 00 00 00 01 21");
	}
	
	// Write/Read DSI
	if (!strncmp(DSICHECKSTATUS,cmd_buf,strlen(DSICHECKSTATUS))){
		printk("MDSS:[mdss_debug.c]:%s:FUNC(%s) envoked!! \n", __func__,DSICHECKSTATUS);
		check_panel_status();
	}
	else if (!strncmp(DSI_WRITE_CMD,cmd_buf,strlen(DSI_WRITE_CMD)))
	{
		char cmd_data[20];
		unsigned int tmp1,tmp2;
		int i;
		int offset=0;
		struct dsi_cmd_desc* dsi_cmd = (struct dsi_cmd_desc*)
			kzalloc(sizeof(struct dsi_cmd_desc),GFP_KERNEL);

		// echo write:cmd val1 val2 val3...

		offset=strlen(DSI_WRITE_CMD);
		sscanf(cmd_buf+offset, "%x", &tmp1); dsi_cmd->dchdr.dtype = tmp1;offset+= 3;
		sscanf(cmd_buf+offset, "%x", &tmp1); dsi_cmd->dchdr.last = tmp1;offset+= 3;
		sscanf(cmd_buf+offset, "%x", &tmp1); dsi_cmd->dchdr.vc = tmp1;offset+= 3;
		sscanf(cmd_buf+offset, "%x", &tmp1); dsi_cmd->dchdr.ack = tmp1;offset+= 3;
		sscanf(cmd_buf+offset, "%x %x", &tmp1,&tmp2); offset+= 6;
		dsi_cmd->dchdr.wait = tmp1 + (tmp2 << 8);
		sscanf(cmd_buf+offset, "%x", &tmp1); dsi_cmd->dchdr.dlen = tmp1,offset+= 3;
		dsi_cmd->payload = cmd_data;

		printk("MDSS:[mdss_debug.c]:%s:write >>>> \n", __func__);
		printk("MDSS:MIPI cmd dtype=0x%x,last=0x%x,vc=0x%x,ack=0x%x,wait=0x%x(%d),dlen=0x%x(%d)\n",
				dsi_cmd->dchdr.dtype,dsi_cmd->dchdr.last,dsi_cmd->dchdr.vc,dsi_cmd->dchdr.ack,dsi_cmd->dchdr.wait,dsi_cmd->dchdr.wait,
				dsi_cmd->dchdr.dlen,dsi_cmd->dchdr.dlen);

		for (i=0;i<dsi_cmd->dchdr.dlen;i++){
			unsigned int value;
			sscanf(cmd_buf+offset, "%x", &value); offset+= 3;
			cmd_data[i] = (unsigned char)value;
			printk("MDSS:dsi_cmd cmd_data[%d]=0x%x\n",i,cmd_data[i]);
		}

		_send_mipi_write_cmd(dsi_cmd);

		if (dsi_cmd) kzfree(dsi_cmd);
	}
	else if (!strncmp(DSI_READ_CMD,cmd_buf,strlen(DSI_READ_CMD)))
	{
#if 0
		char cmd_data[10];
		unsigned int tmp1,tmp2;
		int i;
		int offset=0;
		struct dsi_cmd_desc* dsi_cmd = (struct dsi_cmd_desc*)
			kzalloc(sizeof(struct dsi_cmd_desc),GFP_KERNEL);

		offset=strlen(DSI_READ_CMD);
		sscanf(cmd_buf+offset, "%x", &tmp1); dsi_cmd->dchdr.dtype = tmp1;offset+= 3;
		sscanf(cmd_buf+offset, "%x", &tmp1); dsi_cmd->dchdr.last = tmp1;offset+= 3;
		sscanf(cmd_buf+offset, "%x", &tmp1); dsi_cmd->dchdr.vc = tmp1;offset+= 3;
		sscanf(cmd_buf+offset, "%x", &tmp1); dsi_cmd->dchdr.ack = tmp1;offset+= 3;
		sscanf(cmd_buf+offset, "%x %x", &tmp1,&tmp2); offset+= 6;
		dsi_cmd->dchdr.wait = tmp1 + (tmp2 << 8);
		sscanf(cmd_buf+offset, "%x", &tmp1); dsi_cmd->dchdr.dlen = tmp1,offset+= 3;
		dsi_cmd->payload = cmd_data;

		printk("MDSS:[mdss_debug.c]:%s:read >>>> \n", __func__);
		printk("MDSS:MIPI cmd dtype=0x%x,last=0x%x,vc=0x%x,ack=0x%x,wait=0x%x(%d),dlen=0x%x(%d)\n",
				dsi_cmd->dchdr.dtype,dsi_cmd->dchdr.last,dsi_cmd->dchdr.vc,dsi_cmd->dchdr.ack,dsi_cmd->dchdr.wait,dsi_cmd->dchdr.wait,
				dsi_cmd->dchdr.dlen,dsi_cmd->dchdr.dlen);

		for (i=0;i<dsi_cmd->dchdr.dlen;i++){
			unsigned int value;
			sscanf(cmd_buf+offset, "%x", &value); offset+= 3;
			cmd_data[i] = (unsigned char)value;
			printk("MDSS:dsi_cmd cmd_data[%d]=0x%x\n",i,cmd_data[i]);
		}

		_send_mipi_read_cmd(dsi_cmd);

		if (dsi_cmd) kzfree(dsi_cmd);
#else
		unsigned int cmd0,cmd1;
		int offset=0;
		offset=strlen(DSI_READ_CMD);
		sscanf(cmd_buf+offset, "%x %x", &cmd0,&cmd1);
		printk("MDSS:[mdss_debug.c]:%s:read >>>> cmd0=0x%x,cmd1=0x%x\n", __func__,cmd0,cmd1);

		send_mipi_read_cmd(cmd0,cmd1);
#endif
	}
	else if (!strncmp(SET_LOGFLAG,cmd_buf,strlen(SET_LOGFLAG))){
		unsigned int flag = 0;
		sscanf(cmd_buf+strlen(SET_LOGFLAG), "0x%x", &flag);
		printk("MDSS:[mdss_debug.c]:%s:FUNC(%s) envoked!! flag=0x%x\n", __func__,SET_LOGFLAG,flag);
		set_amdu_logflag(flag);

	}
	else if (!strncmp(CLR_LOGFLAG,cmd_buf,strlen(CLR_LOGFLAG))){
		unsigned int flag = 0;
		sscanf(cmd_buf+strlen(CLR_LOGFLAG), "0x%x", &flag);
		printk("MDSS:[mdss_debug.c]:%s:FUNC(%s) envoked!! flag=0x%x\n", __func__,CLR_LOGFLAG,flag);
		clr_amdu_logflag(flag);
	}
	else if (!strncmp("ambient=",cmd_buf,strlen("ambient="))){
		int val;
		sscanf(cmd_buf+strlen("ambient="), "%d", &val);
		amdu_data.panel_status.ambient = (val != 0);
		printk("MDSS:[mdss_debug.c]:%s:amdu_data.panel_status.ambient = 0x%x\n", __func__,amdu_data.panel_status.ambient);
	}
	else if (!strncmp("panelmsg=",cmd_buf,strlen("panelmsg="))){
		show_panel_message(cmd_buf+strlen("panelmsg="));
		printk("MDSS:[mdss_debug.c]:%s:panelmsg=%s\n", __func__,cmd_buf+strlen("panelmsg="));
	}

	printk("MDSS:[mdss_debug.c]:%s:---\n", __func__);

	return count;
}

static ssize_t mdss_debug_base_cmd_read(struct file *file,
			char __user *buff, size_t count, loff_t *ppos)
{
	int len, tot;
	char buf[512];

	if (*ppos)
		return 0;	/* the end */

	len = sizeof(buf);

	tot = scnprintf(buf, len, "\nAMDU STATUS:\n");

	printk("You may say:\n");
	tot += scnprintf(buf + tot, len - tot, "You may say:\n");

	printk("=======================================\n");
	tot += scnprintf(buf + tot, len - tot, "=======================================\n");
	printk("MIPI CMDS:\n");	
	tot += scnprintf(buf + tot, len - tot, "MIPI CMDS:\n");
	printk("	%s\n",DSI_CMD_NORON);
	tot += scnprintf(buf + tot, len - tot, "	%s\n",DSI_CMD_NORON);
	printk("	%s\n",DSI_CMD_ALLPON);
	tot += scnprintf(buf + tot, len - tot, "	%s\n",DSI_CMD_ALLPON);
	printk("	%s\n",DSI_CMD_SLPIN);
	tot += scnprintf(buf + tot, len - tot, "	%s\n",DSI_CMD_SLPIN);
	printk("	%s\n",DSI_CMD_SLPOUT);
	tot += scnprintf(buf + tot, len - tot, "	%s\n",DSI_CMD_SLPOUT);
	printk("	%s\n",DSI_CMD_DISPOFF);
	tot += scnprintf(buf + tot, len - tot, "	%s\n",DSI_CMD_DISPOFF);
	printk("	%s\n",DSI_CMD_DISPON);
	tot += scnprintf(buf + tot, len - tot, "	%s\n",DSI_CMD_DISPON);
	printk("	%s\n",STICKING_SOL1);
	tot += scnprintf(buf + tot, len - tot, "	%s\n",STICKING_SOL1);
	printk("---------------------------------------\n");
	tot += scnprintf(buf + tot, len - tot, "---------------------------------------\n");
	printk("DEBUG CMDS:\n");
	tot += scnprintf(buf + tot, len - tot, "DEBUG CMDS:\n");
	printk("	%s0x??\n",SET_LOGFLAG);
	tot += scnprintf(buf + tot, len - tot, "	%s0x??\n",SET_LOGFLAG);
	printk("	%s0x??\n",CLR_LOGFLAG);
	tot += scnprintf(buf + tot, len - tot, "	%s0x??\n",CLR_LOGFLAG);
	printk("	%s\n",DSICHECKSTATUS);
	tot += scnprintf(buf + tot, len - tot, "	%s\n",DSICHECKSTATUS);
	printk("	%s\n",DSI_WRITE_CMD);
	tot += scnprintf(buf + tot, len - tot, "	%s\n",DSI_WRITE_CMD);
	printk("	%s\n",DSI_READ_CMD);
	tot += scnprintf(buf + tot, len - tot, "	%s\n",DSI_READ_CMD);
	printk("=======================================\n");
	tot += scnprintf(buf + tot, len - tot, "=======================================\n");

	tot += scnprintf(buf + tot, len - tot, "\n");
	if (copy_to_user(buff, buf, len))
		return -EFAULT;

	*ppos += tot;

	return tot;
}

static const struct file_operations mdss_cmd_fops = {
	.open = mdss_debug_cmd_open,
	.release = mdss_debug_cmd_release,
	.read = mdss_debug_base_cmd_read,
	.write = mdss_debug_base_cmd_write,
};

////////////////////////////////////////////////////////////////////////////////
// Watch DOG task

static void mdss_debug_watchdog(struct work_struct *work)
{
	// Handle things about
	int sec_not_update = jiffies_to_msecs(jiffies - amdu_data.panel_status.last_time_update)/1000;
	static int pixel_invert_count = 0;

	if (sec_not_update > 60 && (sec_not_update % 5 == 0))
		printk("Framebuffer has not updated for %d sec, ambient=%d, timeout = %d\n",sec_not_update,amdu_data.panel_status.ambient,amdu_data.watch_dog_data.dim_time_out);

	if (sec_not_update > amdu_data.watch_dog_data.dim_time_out)
	{
		int dim_val = amdu_data.fb_status.mfd->bl_level - (sec_not_update - amdu_data.watch_dog_data.dim_time_out);
		if(dim_val > 0)
		{
			amdu_data.panel_status.auto_dim = true;
#ifdef ASUS_FACTORY_BUILD
			printk("[AMDU] Skip dum backlight due %d to factory build.\n;",dim_val);
#else
			amdu_data.ctrl->panel_data.set_backlight(&(amdu_data.ctrl->panel_data), dim_val);
#endif
		}
	}else
	{
		if (amdu_data.panel_status.auto_dim)
		{
#ifdef ASUS_FACTORY_BUILD
			printk("[AMDU] Skip restore backlight due %d to factory build.\n;",amdu_data.fb_status.mfd->bl_level);
#else
			amdu_data.ctrl->panel_data.set_backlight(&(amdu_data.ctrl->panel_data), amdu_data.fb_status.mfd->bl_level);
#endif
			amdu_data.panel_status.auto_dim = false;
		}
	}

	if (stick_sol1_on)
	{
		if (amdu_data.panel_status.ambient && (pm8226_get_prop_batt_status() == 1))
		{
			if (pixel_invert_count == 0)
				pixel_invert_proc();
		}
		if ( !amdu_data.panel_status.ambient && pixel_invert)
			pixel_invert_proc();

		pixel_invert_count++;
		pixel_invert_count%=5;
	}

	schedule_delayed_work(&(amdu_data.watch_dog_data.watch_dog),msecs_to_jiffies(amdu_data.watch_dog_data.watch_dog_interval));
}



static int fb_event_callback(struct notifier_block *self,
				unsigned long event, void *data)
{
	struct fb_event *evdata = data;

	amdu_data.fb_status.mfd = evdata->info->par;

	if (event == FB_EVENT_BLANK && evdata) {
		int *blank = evdata->data;
		switch (*blank) {
		case FB_BLANK_UNBLANK:
			amdu_data.fb_status.blank = 0;
			amdu_data.panel_status.last_time_update = 0;
			schedule_delayed_work(&amdu_data.watch_dog_data.watch_dog,
				msecs_to_jiffies(amdu_data.watch_dog_data.watch_dog_interval));
			break;
		case FB_BLANK_POWERDOWN:
			amdu_data.fb_status.blank = 1;
			amdu_data.panel_status.last_time_update = 0;
			cancel_delayed_work(&amdu_data.watch_dog_data.watch_dog);
			break;
		}
	}
	return 0;
}

int mdss_debug_watch_dog_init(void)
{
	int rc = 0;
	struct watch_dog_data* pwatch_dog_data;

	amdu_data.watch_dog_data.watch_dog_interval = WATCH_DOG_INTERVAL;
	amdu_data.watch_dog_data.dim_time_out = DIM_TIME_OUT;
	amdu_data.watch_dog_data.fb_notifier.notifier_call = fb_event_callback;

	rc = fb_register_client(&amdu_data.watch_dog_data.fb_notifier);
	if (rc < 0) {
		pr_err("%s: fb_register_client failed, returned with rc=%d\n",
								__func__, rc);
		kfree(pwatch_dog_data);
		return -EPERM;
	}

	pr_info("%s: Watch dog interval:%d\n", __func__,amdu_data.watch_dog_data.watch_dog_interval);

	INIT_DELAYED_WORK(&amdu_data.watch_dog_data.watch_dog, mdss_debug_watchdog);

	pr_debug("%s: Watch Dog work queue initialized\n", __func__);

	return rc;
}


////////////////////////////////////////////////////////////////////////////////
// INIT

int create_amdu_debugfs(struct dentry *parent)
{
//	amdu_data.debug_log_flag = 0xFF;
	amdu_data.panel_status.ambient = 0;

	amdu_data.ent_reg = debugfs_create_file("amdu_cmd", 0644, parent, 0/* we can transfer some data here*/, &mdss_cmd_fops);
	if (IS_ERR_OR_NULL(amdu_data.ent_reg)) {
		printk("[AMDU] debugfs_create_file: cmd fail !!\n");
		return -EINVAL;
	}

	//mdss_debug_watch_dog_init();

	return 0;
}



////////////////////////////////////////////////////////////////////////////////
// DSI COMMANDS
// Read Number of Error on DSI
//static char RDNUMED = 0x05;

static int check_panel_status(void)
{
	volatile unsigned char val = 0;

	if (!amdu_data.ctrl){
		printk("MDSS:[mdss_dsi_panel.c]:%s: ERR!! amdu_data.ctrl = 0 !!\n",__func__);
		return -ENODEV;
	}

#ifdef TRACE_FLOW
	printk("MDSS:[mdss_asus_debug.c]:%s: +++\n", __func__);
#endif


	// Read Display Image Mode (RDDIM)
	val = send_mipi_read_cmd(0x0D,0);
	printk("MDSS:[mdss_dsi_panel.c]:%s:RDDIM=0x%x!!\n",__func__,val);
	printk("	RDDIM:D5=%d:INV ON\n", val & (1<<5) ? 1 : 0);
	printk("	RDDIM:D4=%d:ALLPX ON\n", val & (1<<4) ? 1 : 0);
	printk("	RDDIM:D3=%d:ALLPX OFF\n", val & (1<<3) ? 1 : 0);
	printk("	RDDIM:D2~D0=%d:GCS\n", val & 0x07);

	// Read Display Power Mode (RDDPM)
	val = send_mipi_read_cmd(0x0A,0);
	printk("MDSS:[mdss_dsi_panel.c]:%s:RDDPM=0x%x!!\n",__func__,val);
	printk("	RDDPM:D7=%d:BST ON\n", val & (1<<7) ? 1 : 0);
	printk("	RDDPM:D4=%d:SLP OUT\n", val & (1<<4) ? 1 : 0);
	printk("	RDDPM:D2=%d:DIS ON\n", val & (1<<2) ? 1 : 0);

	// Read Display Signal Mode (RDDSM)
	val = send_mipi_read_cmd(0x0E,0);
	printk("MDSS:[mdss_dsi_panel.c]:%s:RDDSM=0x%x!!\n",__func__,val);
	printk("	RDDSM:D7=%d:TEON, Tearing Effect Line On/Off\n", val & (1<<7) ? 1 : 0);
	printk("	RDDSM:D6=%d:TELOM, Tearing effect line mode\n", val & (1<<6) ? 1 : 0);

#if 0
	// Read Display Self-Diagnostic Result (RDDSDR)
	val = send_mipi_read_cmd(0x0F,0);
	printk("MDSS:[mdss_dsi_panel.c]:%s:RDDSDR=0x%x!!\n",__func__,val);
	printk("	RDDSDR:D7=%d:RELD, Register Loading Detection\n", val & (1<<7) ? 1 : 0);
	printk("	RDDSDR:D6=%d:FUND, Functionality Detection\n", val & (1<<6) ? 1 : 0);
#endif

#ifdef TRACE_FLOW
	printk("MDSS:[mdss_asus_debug.c]:%s: ---\n", __func__);
#endif

	printk("amdu_data.panel_status.ambient = %d\n",amdu_data.panel_status.ambient);
	printk("amdu_data.debug_str = %s\n",amdu_data.debug_str);

	return 0;
}

void pixel_invert_proc(void)
{

	char cmd_data[10];
	unsigned int tmp1,tmp2;
	int i;
	int offset=0;
	unsigned int value;	
	struct dsi_cmd_desc* dsi_cmd = (struct dsi_cmd_desc*)
		kzalloc(sizeof(struct dsi_cmd_desc),GFP_KERNEL);
	memset(cmd_buf,0,CMD_BUF_SIZE);
	// Build Commands
	if (pixel_invert == false)
	{
		// 21 INVON
		printk("echo > 'write:05 01 00 00 00 00 01 21' > /d/mdp/amdu_cmd\n");
		strcpy(cmd_buf,"write:05 01 00 00 00 00 01 21");
		pixel_invert =true;
	}else{
		// 20 INVOFF
		printk("echo > 'write:05 01 00 00 00 00 01 20' > /d/mdp/amdu_cmd\n");
		strcpy(cmd_buf,"write:05 01 00 00 00 00 01 20");
		pixel_invert =false;
	}	

	offset=strlen(DSI_WRITE_CMD);
	sscanf(cmd_buf+offset, "%x", &tmp1); dsi_cmd->dchdr.dtype = tmp1;offset+= 3;
	sscanf(cmd_buf+offset, "%x", &tmp1); dsi_cmd->dchdr.last = tmp1;offset+= 3;
	sscanf(cmd_buf+offset, "%x", &tmp1); dsi_cmd->dchdr.vc = tmp1;offset+= 3;
	sscanf(cmd_buf+offset, "%x", &tmp1); dsi_cmd->dchdr.ack = tmp1;offset+= 3;
	sscanf(cmd_buf+offset, "%x %x", &tmp1,&tmp2); offset+= 6;
	dsi_cmd->dchdr.wait = tmp1 + (tmp2 << 8);
	sscanf(cmd_buf+offset, "%x", &tmp1); dsi_cmd->dchdr.dlen = tmp1,offset+= 3;
	dsi_cmd->payload = cmd_data;

	printk("MDSS:[mdss_debug.c]:%s:write >>>> \n", __func__);
	printk("MDSS:MIPI cmd dtype=0x%x,last=0x%x,vc=0x%x,ack=0x%x,wait=0x%x(%d),dlen=0x%x(%d)\n",
			dsi_cmd->dchdr.dtype,dsi_cmd->dchdr.last,dsi_cmd->dchdr.vc,dsi_cmd->dchdr.ack,dsi_cmd->dchdr.wait,dsi_cmd->dchdr.wait,
			dsi_cmd->dchdr.dlen,dsi_cmd->dchdr.dlen);

	for (i=0;i<dsi_cmd->dchdr.dlen;i++){
		sscanf(cmd_buf+offset, "%x", &value); offset+= 3;
		cmd_data[i] = (unsigned char)value;
		printk("MDSS:dsi_cmd cmd_data[%d]=0x%x\n",i,cmd_data[i]);
	}

	_send_mipi_write_cmd(dsi_cmd);

	if (dsi_cmd) kzfree(dsi_cmd);
}
////////////////////////////////////////////////////////////////////////////////
// MIPI Commands
int send_mipi_write_cmd(char cmd0,char cmd1)
{
// Todo: Need porting. A convenient way to try mipi command.
#if 0
	char cmd_data[10];
	unsigned int tmp1,tmp2;
	int i;
	int offset=0;
	struct dsi_cmd_desc* dsi_cmd = (struct dsi_cmd_desc*)
		kzalloc(sizeof(struct dsi_cmd_desc),GFP_KERNEL);

	// echo write:cmd val1 val2 val3...

	offset=strlen(DSI_WRITE_CMD);
	sscanf(cmd_buf+offset, "%x", &tmp1); dsi_cmd->dchdr.dtype = tmp1;offset+= 3;
	sscanf(cmd_buf+offset, "%x", &tmp1); dsi_cmd->dchdr.last = tmp1;offset+= 3;
	sscanf(cmd_buf+offset, "%x", &tmp1); dsi_cmd->dchdr.vc = tmp1;offset+= 3;
	sscanf(cmd_buf+offset, "%x", &tmp1); dsi_cmd->dchdr.ack = tmp1;offset+= 3;
	sscanf(cmd_buf+offset, "%x %x", &tmp1,&tmp2); offset+= 6;
	dsi_cmd->dchdr.wait = tmp1 + (tmp2 << 8);
	sscanf(cmd_buf+offset, "%x", &tmp1); dsi_cmd->dchdr.dlen = tmp1,offset+= 3;
	dsi_cmd->payload = cmd_data;

	printk("MDSS:[mdss_debug.c]:%s:write >>>> \n", __func__);
	printk("MDSS:MIPI cmd dtype=0x%x,last=0x%x,vc=0x%x,ack=0x%x,wait=0x%x(%d),dlen=0x%x(%d)\n",
			dsi_cmd->dchdr.dtype,dsi_cmd->dchdr.last,dsi_cmd->dchdr.vc,dsi_cmd->dchdr.ack,dsi_cmd->dchdr.wait,dsi_cmd->dchdr.wait,
			dsi_cmd->dchdr.dlen,dsi_cmd->dchdr.dlen);

	for (i=0;i<dsi_cmd->dchdr.dlen;i++){
		unsigned int value;
		sscanf(cmd_buf+offset, "%x", &value); offset+= 3;
		cmd_data[i] = (unsigned char)value;
		printk("MDSS:dsi_cmd cmd_data[%d]=0x%x\n",i,cmd_data[i]);
	}

	_send_mipi_write_cmd(dsi_cmd);

	if (dsi_cmd) kzfree(dsi_cmd);
#else
	return 0;
#endif
}


int _send_mipi_write_cmd(struct dsi_cmd_desc* test_cmd)
{

	struct mdss_dsi_ctrl_pdata *ctrl = amdu_data.ctrl;
	struct dcs_cmd_req cmdreq;	

	if (!amdu_data.ctrl){
		printk("MDSS:[mdss_asus_debug.c]:%s: ERR!! amdu_data.ctrl = 0 !!\n",__func__);
		return -ENODEV;
	}

	memset(&cmdreq, 0, sizeof(cmdreq));
	cmdreq.cmds = test_cmd;
	cmdreq.cmds_cnt = 1;
	cmdreq.flags = CMD_REQ_COMMIT | CMD_CLK_CTRL;
	cmdreq.rlen = 0;
	cmdreq.cb = NULL;

	mdss_dsi_cmdlist_put(ctrl, &cmdreq);
	return 0;
}


static char dcs_cmd[2] = {0x54, 0x00}; /* DTYPE_DCS_READ */
static struct dsi_cmd_desc dcs_read_cmd = {
	{DTYPE_DCS_READ, 1, 0, 1, 5, sizeof(dcs_cmd)},
	dcs_cmd
};

static u32 send_mipi_read_cmd_len;
static void send_mipi_read_cmd_cb(int len)
{
	send_mipi_read_cmd_len = len;
}
static char send_mipi_read_cmd_data[16];

int send_mipi_read_cmd(	char cmd0, char cmd1)
{
	struct mdss_dsi_ctrl_pdata *ctrl = amdu_data.ctrl;
	struct dcs_cmd_req cmdreq;
	int i;

	if (!amdu_data.ctrl){
		printk("MDSS:[mdss_asus_debug.c]:%s: ERR!! amdu_data.ctrl = 0 !!\n",__func__);
		return -ENODEV;
	}

	dcs_cmd[0] = cmd0;
	dcs_cmd[1] = cmd1;
	memset(&cmdreq, 0, sizeof(cmdreq));
	cmdreq.cmds = &dcs_read_cmd;
	cmdreq.cmds_cnt = 1;
	cmdreq.flags = CMD_REQ_RX | CMD_REQ_COMMIT;
	cmdreq.rlen = 16;
	cmdreq.rbuf = send_mipi_read_cmd_data;
	cmdreq.cb = send_mipi_read_cmd_cb; /* call back */
	mdss_dsi_cmdlist_put(ctrl, &cmdreq);
	/*
	 * blocked here, until call back called
	 */

	for(i=0;i<send_mipi_read_cmd_len;i++){
		printk("MDSS:[mdss_asus_debug.c]:%s: MIPI Read data[%d] = %d(0x%x)\n",__func__,i,send_mipi_read_cmd_data[i],send_mipi_read_cmd_data[i]);
	}

	return send_mipi_read_cmd_data[0];
}



////////////////////////////////////////////////////////////////////////////////
// LOG 


unsigned int set_amdu_logflag(unsigned int new_flag)
{
	amdu_data.debug_log_flag |= new_flag;
	return amdu_data.debug_log_flag ;	
}
unsigned int clr_amdu_logflag(unsigned int new_flag)
{
	amdu_data.debug_log_flag &= (~new_flag);
	return amdu_data.debug_log_flag ;
}
unsigned int get_amdu_logflag()
{
	return amdu_data.debug_log_flag ;
}

// Log DSI commands for LK porting"
static int cmd_counter = -1;
// ASUS_BSP +++ Tingyi "[ROBIN][MDSS] Be able to send debug MIPI cmd to MDSS"
void amdu_register_ctrl_pdata(struct mdss_dsi_ctrl_pdata *ctrl_pdata)
{
	amdu_data.ctrl = ctrl_pdata;
}
// ASUS_BSP --- Tingyi "[ROBIN][MDSS] Be able to send debug MIPI cmd to MDSS"
int notify_amdu_panel_on_cmds_start(struct mdss_dsi_ctrl_pdata *ctrl)
{
	// keep the DSI control
	amdu_data.ctrl = ctrl;

	if (!(get_amdu_logflag() & AMDU_DEBUGFAGL_LOGONOFFCMD)) 
		return 1;

	cmd_counter = 0;
	printk("////////////////////////////////////////////////////////////////////////////////\n");
	return 0;
}
extern int mdss_dsi_get_active_panel_type(void);
int notify_amdu_panel_on_cmds_stop(void)
{
	int i;
	if (get_amdu_logflag() & AMDU_DEBUGFAGL_LOGONOFFCMD)
	{
		printk("\n	static struct mipi_dsi_cmd asus_panel_on_cmds[] = {\n");
		for (i=0;i<cmd_counter;i++)
			printk("		{sizeof(disp_on%d), (char *)disp_on%d},\n",i,i);
	
		printk("};\n");
		cmd_counter = -1;
		printk("////////////////////////////////////////////////////////////////////////////////\n");
	}
	
	return 0;
}
int notify_amdu_dsi_cmd_dma_tx(struct dsi_buf *tp)
{
	if (!(get_amdu_logflag() & AMDU_DEBUGFAGL_LOGONOFFCMD)) 
		return 1;	
	if (cmd_counter>=0)
	{
		int i;	
		printk("static char disp_on%d[%d] = {",cmd_counter,tp->len);
		for(i=0;i<tp->len;i++)
			printk("0x%02X, ",tp->data[i]);
		printk("};\n");
		cmd_counter ++;
	}	
	return 0;
}

void set_amdu_fbinfo(struct fb_info *info)
{
	amdu_data.fb_status.fb_info = info;
	return;
}


void show_panel_message(char* msg)
{
	static char panel_msg[128];
	char *envp[2] = {panel_msg, NULL};
	sprintf(panel_msg,"PANEL_MSG=%s",msg);
	if (amdu_data.fb_status.fb_info){
		kobject_uevent_env(
			&amdu_data.fb_status.fb_info->dev->kobj,KOBJ_CHANGE, envp);
		printk("MDSS:DEBUG:%s: msg=%s\n",__func__,msg);
	}else{
		printk("MDSS:DEBUG:%s: ERR! amdu_data.fb_status.fb_info = 0\n\n",__func__);
	}
	return;
}

void notify_amdu_panel_power_mode(int mode)
{
	static char panel_msg[32];
	char *envp[2] = {panel_msg, NULL};
	sprintf(panel_msg,"PANEL_LOWPOWER=%s",(mode == PANEL_POWER_LOW ?"1":"0"));
	if (amdu_data.fb_status.fb_info){
		kobject_uevent_env(
			&amdu_data.fb_status.fb_info->dev->kobj,KOBJ_CHANGE, envp);
		printk("MDSS:DEBUG:%s: msg=%s\n",__func__,panel_msg);
	}else{
		printk("MDSS:DEBUG:%s: ERR! amdu_data.fb_status.fb_info = 0\n\n",__func__);
	}
	amdu_data.panel_status.power_mode = mode;
	return;
}
void notify_amdu_panel_ambient_on(enable)
{
	amdu_data.panel_status.ambient = enable;
	return;
}
// ASUS_BSP +++ Tingyi "[ROBIN][MDSS] Always entering ambient mode in factory build."
extern int enable_ambient(int enable);
void notify_amdu_overlay_commit(void)
{
	amdu_data.panel_status.last_time_update = jiffies;

#ifdef ASUS_FACTORY_BUILD
	{
		static int cnt = 10;
		if (cnt){
			cnt --;
			if (cnt == 0)
				enable_ambient(1);
		}
	}
#endif


}
// ASUS_BSP --- Tingyi "[ROBIN][MDSS] Always entering ambient mode in factory build."


void set_debug_str(char* debug_str)
{
	strcpy(amdu_data.debug_str,debug_str);
}
