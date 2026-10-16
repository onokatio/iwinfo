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

const char *iwinfo_qcawificfg80211_ifname(const char *name)
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
	const char *vif = iwinfo_qcawificfg80211_ifname(name); \
	if (!vif) return -1; \
	return !nl80211_ops.op(vif, buf) ? 0 : (wext_ops.op ? wext_ops.op(vif, buf) : -1); \
}
#define QCA_STR(op) \
static int qca_##op(const char *name, char *buf) { \
	const char *vif = iwinfo_qcawificfg80211_ifname(name); \
	return vif ? nl80211_ops.op(vif, buf) : -1; \
}
#define QCA_LIST(op) \
static int qca_##op(const char *name, char *buf, int *len) { \
	const char *vif = iwinfo_qcawificfg80211_ifname(name); \
	return vif ? nl80211_ops.op(vif, buf, len) : -1; \
}
QCA_NUM(center_chan1) QCA_NUM(center_chan2) QCA_NUM(htmode)
QCA_NUM(mode) QCA_NUM(channel) QCA_NUM(frequency) QCA_NUM(frequency_offset)
QCA_NUM(txpower_offset) QCA_NUM(signal)
QCA_NUM(noise) QCA_NUM(quality) QCA_NUM(quality_max) QCA_NUM(mbssid_support)
QCA_STR(ssid) QCA_STR(bssid) QCA_STR(hardware_id)
QCA_STR(hardware_name) QCA_STR(encryption) QCA_STR(phyname) QCA_STR(lookup_phy)
QCA_LIST(txpwrlist) QCA_LIST(freqlist) QCA_LIST(countrylist) QCA_LIST(survey)

static int qca_txpower(const char *name, int *buf)
{
	const char *vif = iwinfo_qcawificfg80211_ifname(name);
	if (!vif) return -1;
	/* QSDK's WEXT value agrees with its configured dBm; nl80211 can report bogus values. */
	return !wext_ops.txpower(vif, buf) ? 0 : nl80211_ops.txpower(vif, buf);
}

static int qca_parse_country(const char *line, char *buf)
{
	const char *value = strchr(line, ':');
	if (!value || !isupper((unsigned char)value[1]) ||
	    !isupper((unsigned char)value[2]) || (unsigned char)value[3] > ' ') return -1;
	buf[0] = value[1]; buf[1] = value[2]; buf[2] = 0;
	return 0;
}

static int qca_country(const char *name, char *buf)
{
	char radio[IFNAMSIZ], command[96], line[128];
	const char *vif;
	FILE *fp;
	int result = -1;
	if (qca_radio(name, radio)) return -1;
	/* QSDK keeps a per-radio country while nl80211 reports the global world domain. */
	snprintf(command, sizeof(command), "/usr/sbin/cfg80211tool %s getCountry 2>/dev/null", radio);
	fp = popen(command, "r");
	if (fp) {
		if (fgets(line, sizeof(line), fp)) result = qca_parse_country(line, buf);
		if (pclose(fp)) result = -1;
	}
	if (!result) return 0;
	vif = iwinfo_qcawificfg80211_ifname(name);
	return vif ? nl80211_ops.country(vif, buf) : -1;
}

static int qca_parse_station(const char *line, struct iwinfo_assoclist_entry *entry)
{
	char mac[18], tx[8], rx[8];
	const char *mode, *width;
	unsigned int mhz, rx_nss, tx_nss;
	unsigned long tx_rate, rx_rate;
	int signal, i;
	if (sscanf(line, "%17s %*u %*u %7[0-9]M %7[0-9]M %4d", mac, tx, rx, &signal) != 4 ||
	    strlen(mac) != 17 || signal < -128 || signal > 0) return -1;
	tx_rate = strtoul(tx, NULL, 10);
	rx_rate = strtoul(rx, NULL, 10);
	if (tx_rate > UINT32_MAX / 1000 || rx_rate > UINT32_MAX / 1000) return -1;
	for (i = 0; i < 17; i++)
		if (i % 3 == 2 ? mac[i] != ':' : !isxdigit((unsigned char)mac[i])) return -1;
	memset(entry, 0, sizeof(*entry));
	for (i = 0; i < 6; i++) entry->mac[i] = strtoul(mac + 3 * i, NULL, 16);
	entry->signal = signal;
	entry->rx_rate.rate = rx_rate * 1000;
	entry->tx_rate.rate = tx_rate * 1000;
	entry->rx_rate.mcs = entry->tx_rate.mcs = -1;
	entry->rx_rate.he_gi = entry->tx_rate.he_gi = entry->rx_rate.eht_gi = entry->tx_rate.eht_gi = UINT8_MAX;
	mode = strstr(line, " IEEE80211_MODE_11");
	if (mode) {
		mode++;
		width = strstr(mode, "_EHT");
		if (width) entry->rx_rate.is_eht = entry->tx_rate.is_eht = 1;
		else {
			width = strstr(mode, "_HE");
			if (width) entry->rx_rate.is_he = entry->tx_rate.is_he = 1;
			else {
				width = strstr(mode, "_VHT");
				if (width) entry->rx_rate.is_vht = entry->tx_rate.is_vht = 1;
				else {
					width = strstr(mode, "_HT");
					if (width) entry->rx_rate.is_ht = entry->tx_rate.is_ht = 1;
				}
			}
		}
		if (width && sscanf(width + (width[1] == 'H' ? 3 : 4), "%u", &mhz) == 1 &&
		    (mhz == 20 || mhz == 40 || mhz == 80 || mhz == 160 || mhz == 320)) {
			entry->rx_rate.mhz = entry->tx_rate.mhz = mhz % 256;
			entry->rx_rate.mhz_hi = entry->tx_rate.mhz_hi = mhz / 256;
		}
		mode = strchr(mode, ' ');
		if (mode && sscanf(mode, "%u %u", &rx_nss, &tx_nss) == 2 &&
		    rx_nss >= 1 && rx_nss <= 8 && tx_nss >= 1 && tx_nss <= 8) {
			entry->rx_rate.nss = rx_nss;
			entry->tx_rate.nss = tx_nss;
		}
	}
	return 0;
}

static int qca_assoclist(const char *name, char *buf, int *len)
{
	const char *vif = iwinfo_qcawificfg80211_ifname(name);
	char command[96], line[512];
	struct iwinfo_assoclist_entry entry, native;
	FILE *fp;
	int noise = 0, ch;
	*len = 0;
	if (!vif) return -1;
	if (!nl80211_ops.assoclist(vif, buf, len) && *len > 0) return 0;
	*len = 0;
	/* QSDK can return an empty nl80211 dump while its native station list is populated. */
	if (!qca_name(vif) || vif[0] == '-') return -1;
	snprintf(command, sizeof(command), "/usr/sbin/wlanconfig %s list sta 2>/dev/null", vif);
	fp = popen(command, "r");
	if (!fp) return -1;
	qca_noise(vif, &noise);
	while (fgets(line, sizeof(line), fp)) {
		if (!strchr(line, '\n') && !feof(fp)) {
			while ((ch = fgetc(fp)) != '\n' && ch != EOF) { }
			continue;
		}
		if (qca_parse_station(line, &entry) || *len + sizeof(entry) > IWINFO_BUFSIZE) continue;
		if (!iwinfo_nl80211_get_station(vif, entry.mac, &native)) {
			/* QSDK implements individual GET_STATION requests, but not station dumps. */
			if (!native.rx_rate.mhz) native.rx_rate.mhz = entry.rx_rate.mhz;
			if (!native.tx_rate.mhz) native.tx_rate.mhz = entry.tx_rate.mhz;
			if (!native.rx_rate.is_ht && !native.rx_rate.is_vht) {
				native.rx_rate.is_he = entry.rx_rate.is_he;
				native.rx_rate.is_eht = entry.rx_rate.is_eht;
				native.rx_rate.is_ht = entry.rx_rate.is_ht;
				native.rx_rate.is_vht = entry.rx_rate.is_vht;
			}
			if (!native.tx_rate.is_ht && !native.tx_rate.is_vht) {
				native.tx_rate.is_he = entry.tx_rate.is_he;
				native.tx_rate.is_eht = entry.tx_rate.is_eht;
				native.tx_rate.is_ht = entry.tx_rate.is_ht;
				native.tx_rate.is_vht = entry.tx_rate.is_vht;
			}
			native.rx_rate.mhz_hi = native.tx_rate.mhz_hi = entry.rx_rate.mhz_hi;
			if (!native.rx_rate.nss) native.rx_rate.nss = entry.rx_rate.nss;
			if (!native.tx_rate.nss) native.tx_rate.nss = entry.tx_rate.nss;
			/* QSDK's nl80211 rate can be nonsensical; use the native station rates. */
			native.rx_rate.rate = entry.rx_rate.rate;
			native.tx_rate.rate = entry.tx_rate.rate;
			entry = native;
		}
		entry.noise = noise;
		memcpy(buf + *len, &entry, sizeof(entry));
		*len += sizeof(entry);
	}
	if (ferror(fp)) { pclose(fp); *len = 0; return -1; }
	if (pclose(fp)) { *len = 0; return -1; }
	return 0;
}

static int qca_bitrate(const char *name, int *buf)
{
	const char *vif = iwinfo_qcawificfg80211_ifname(name);
	if (!vif) return -1;
	if (!nl80211_ops.bitrate(vif, buf) && *buf > 0) return 0;
	if (wext_ops.bitrate(vif, buf) || *buf < 0 || *buf > INT_MAX / 1000) return -1;
	/* QSDK's SIOCGIWRATE reports kbit/s, rather than the WEXT bit/s unit. */
	*buf *= 1000;
	return 0;
}

static int qca_scanlist(const char *name, char *buf, int *len)
{
	const char *vif = iwinfo_qcawificfg80211_ifname(name);
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
	.center_chan1 = qca_center_chan1, .center_chan2 = qca_center_chan2, .htmode = qca_htmode,
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
