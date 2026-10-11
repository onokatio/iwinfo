/* SPDX-License-Identifier: GPL-2.0-only */
/* QSDK exposes nl80211 plus radio/VAP relationships and HE/EHT modes in sysfs. */
#include "iwinfo.h"
#include <limits.h>

static int qca_name(const char *name)
{
	return name && *name && strlen(name) < IFNAMSIZ &&
	       strspn(name, "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") == strlen(name);
}

static int qca_read(const char *name, const char *attr, char *buf, size_t size)
{
	char path[128];
	FILE *fp;
	if (!qca_name(name)) return -1;
	snprintf(path, sizeof(path), "/sys/class/net/%s/%s", name, attr);
	fp = fopen(path, "r");
	if (!fp) return -1;
	if (!fgets(buf, size, fp)) { fclose(fp); return -1; }
	fclose(fp);
	buf[strcspn(buf, "\r\n")] = 0;
	return 0;
}

static int qca_radio(const char *name, char *radio)
{
	char modes[2048];
	if (!qca_read(name, "hwmodes", modes, sizeof(modes))) {
		strcpy(radio, name);
		return 0;
	}
	if (!qca_read(name, "parent", radio, IFNAMSIZ) &&
	    !qca_read(radio, "hwmodes", modes, sizeof(modes))) return 0;
	return -1;
}

static const char *qca_resolve(const char *name)
{
	static char resolved[IFNAMSIZ];
	char parent[IFNAMSIZ], flags[32], path[128], link[128];
	struct dirent *e;
	DIR *dir;
	ssize_t n;
	if (!qca_name(name)) return NULL;
	if (qca_read(name, "hwmodes", link, sizeof(link))) return name;
	dir = opendir("/sys/class/net");
	if (!dir) return NULL;
	resolved[0] = 0;
	while ((e = readdir(dir))) {
		if (!qca_read(e->d_name, "parent", parent, sizeof(parent)) &&
		    !strcmp(parent, name) && !qca_read(e->d_name, "flags", flags, sizeof(flags)) &&
		    (strtoul(flags, NULL, 0) & IFF_UP)) {
			/* Prefer the primary VAP, whose QSDK scan path is reliable. */
			if (!resolved[0] || strlen(e->d_name) < strlen(resolved) ||
			    (strlen(e->d_name) == strlen(resolved) && strcmp(e->d_name, resolved) < 0))
				strcpy(resolved, e->d_name);
		}
	}
	closedir(dir);
	if (resolved[0]) return resolved;
	snprintf(path, sizeof(path), "/sys/class/net/%s/phy80211", name);
	n = readlink(path, link, sizeof(link) - 1);
	if (n < 0) return NULL;
	link[n] = 0;
	name = strrchr(link, '/');
	if (!name || !qca_name(++name)) return NULL;
	strcpy(resolved, name);
	return resolved;
}

static int qca_probe(const char *name) { char radio[IFNAMSIZ]; return !qca_radio(name, radio); }

#define QCA_NUM(op) \
static int qca_##op(const char *name, int *buf) { \
	const char *vif = qca_resolve(name); \
	if (!vif) return -1; \
	return !nl80211_ops.op(vif, buf) ? 0 : wext_ops.op(vif, buf); \
}
#define QCA_STR(op) \
static int qca_##op(const char *name, char *buf) { \
	const char *vif = qca_resolve(name); \
	return vif ? nl80211_ops.op(vif, buf) : -1; \
}
#define QCA_LIST(op) \
static int qca_##op(const char *name, char *buf, int *len) { \
	const char *vif = qca_resolve(name); \
	return vif ? nl80211_ops.op(vif, buf, len) : -1; \
}
QCA_NUM(mode) QCA_NUM(channel) QCA_NUM(frequency) QCA_NUM(frequency_offset)
QCA_NUM(txpower) QCA_NUM(txpower_offset) QCA_NUM(signal)
QCA_NUM(noise) QCA_NUM(quality) QCA_NUM(quality_max) QCA_NUM(mbssid_support)
QCA_STR(ssid) QCA_STR(bssid) QCA_STR(country) QCA_STR(hardware_id)
QCA_STR(hardware_name) QCA_STR(encryption) QCA_STR(phyname) QCA_STR(lookup_phy)
QCA_LIST(assoclist) QCA_LIST(txpwrlist) QCA_LIST(freqlist) QCA_LIST(countrylist) QCA_LIST(survey)

static int qca_bitrate(const char *name, int *buf)
{
	const char *vif = qca_resolve(name);
	if (!vif) return -1;
	if (!nl80211_ops.bitrate(vif, buf) && *buf > 0) return 0;
	if (wext_ops.bitrate(vif, buf) || *buf < 0 || *buf > INT_MAX / 1000) return -1;
	/* QSDK's SIOCGIWRATE reports kbit/s, rather than the WEXT bit/s unit. */
	*buf *= 1000;
	return 0;
}

static int qca_scanlist(const char *name, char *buf, int *len)
{
	const char *vif = qca_resolve(name);
	/* The native WEXT scan preserves the QSDK VAP/MLD lifecycle. */
	return vif && strncmp(vif, "phy", 3) ? wext_ops.scanlist(vif, buf, len) : -1;
}

static void qca_parse_modes(const char *modes, int *hw, int *ht)
{
	int i;
	const struct { const char *token; int ht; } widths[] = {
		{ "_HT20", IWINFO_HTMODE_HT20 }, { "_HT40", IWINFO_HTMODE_HT40 },
		{ "_VHT20", IWINFO_HTMODE_VHT20 }, { "_VHT40", IWINFO_HTMODE_VHT40 },
		{ "_VHT80", IWINFO_HTMODE_VHT80 }, { "_VHT160", IWINFO_HTMODE_VHT160 },
		{ "_HE20", IWINFO_HTMODE_HE20 }, { "_HE40", IWINFO_HTMODE_HE40 },
		{ "_HE80", IWINFO_HTMODE_HE80 }, { "_HE160", IWINFO_HTMODE_HE160 },
		{ "_EHT20", IWINFO_HTMODE_EHT20 }, { "_EHT40", IWINFO_HTMODE_EHT40 },
		{ "_EHT80", IWINFO_HTMODE_EHT80 }, { "_EHT160", IWINFO_HTMODE_EHT160 },
		{ "_EHT320", IWINFO_HTMODE_EHT320 }
	};
	*hw = *ht = 0;
	if (strstr(modes, "11A")) *hw |= IWINFO_80211_A;
	if (!strncmp(modes, "11B ", 4) || strstr(modes, " 11B ")) *hw |= IWINFO_80211_B;
	if (strstr(modes, "11G")) *hw |= IWINFO_80211_G;
	if (strstr(modes, "_HT")) *hw |= IWINFO_80211_N;
	if (strstr(modes, "_VHT")) *hw |= IWINFO_80211_AC;
	if (strstr(modes, "_HE")) *hw |= IWINFO_80211_AX;
	if (strstr(modes, "_EHT")) *hw |= IWINFO_80211_BE;
	for (i = 0; i < sizeof(widths) / sizeof(widths[0]); i++)
		if (strstr(modes, widths[i].token)) *ht |= widths[i].ht;
}
static int qca_modelist(const char *name, int *hw, int *ht)
{
	char radio[IFNAMSIZ], modes[2048];
	if (qca_radio(name, radio) || qca_read(radio, "hwmodes", modes, sizeof(modes))) return -1;
	qca_parse_modes(modes, hw, ht);
	return 0;
}
static int qca_hwmodelist(const char *name, int *buf) { int ht; return qca_modelist(name, buf, &ht); }
static int qca_htmodelist(const char *name, int *buf) { int hw; return qca_modelist(name, &hw, buf); }

static void qca_close(void) { nl80211_ops.close(); wext_ops.close(); }

const struct iwinfo_ops qcawificfg80211_ops = {
	.name = "qcawificfg80211", .probe = qca_probe,
	.mode = qca_mode, .channel = qca_channel, .frequency = qca_frequency,
	.frequency_offset = qca_frequency_offset, .txpower = qca_txpower,
	.txpower_offset = qca_txpower_offset, .bitrate = qca_bitrate,
	.signal = qca_signal, .noise = qca_noise, .quality = qca_quality,
	.quality_max = qca_quality_max, .mbssid_support = qca_mbssid_support,
	.hwmodelist = qca_hwmodelist, .htmodelist = qca_htmodelist,
	.ssid = qca_ssid, .bssid = qca_bssid, .country = qca_country,
	.hardware_id = qca_hardware_id, .hardware_name = qca_hardware_name,
	.encryption = qca_encryption, .phyname = qca_phyname,
	.assoclist = qca_assoclist, .txpwrlist = qca_txpwrlist,
	.scanlist = qca_scanlist, .freqlist = qca_freqlist,
	.countrylist = qca_countrylist, .survey = qca_survey,
	.lookup_phy = qca_lookup_phy, .close = qca_close
};
