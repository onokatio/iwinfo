/* SPDX-License-Identifier: GPL-2.0-only */
#include <assert.h>
#include <stdio.h>
#include <stddef.h>
static FILE *station_pipe(const char *command, const char *mode);
static int station_close(FILE *fp);
#define popen station_pipe
#define pclose station_close
#include "../iwinfo_qcawificfg80211.c"
#undef popen
#undef pclose
static int nl_rate, nl_stations, nl_result, station_calls, station_failure, station_count = 2;
static int individual_result = -1;
int iwinfo_nl80211_get_station(const char *name, const uint8_t *mac, struct iwinfo_assoclist_entry *entry)
{
	memset(entry, 0, sizeof(*entry));
	memcpy(entry->mac, mac, 6);
	entry->signal = -45;
	entry->rx_rate.rate = 2135200;
	entry->rx_rate.mcs = entry->tx_rate.mcs = -1;
	entry->rx_packets = 11;
	return individual_result;
}
static int native_bitrate(const char *name, int *buf) { *buf = nl_rate; return 0; }
static int vendor_bitrate(const char *name, int *buf) { *buf = 2882; return 0; }
static int native_noise(const char *name, int *buf) { *buf = -96; return 0; }
static int native_assoclist(const char *name, char *buf, int *len)
{
	struct iwinfo_assoclist_entry entry = { .signal = -60 };
	*len = nl_stations ? sizeof(entry) : 0;
	if (*len) memcpy(buf, &entry, *len);
	return nl_result;
}
const struct iwinfo_ops nl80211_ops = {
	.bitrate = native_bitrate, .noise = native_noise, .assoclist = native_assoclist
};
const struct iwinfo_ops wext_ops = { .bitrate = vendor_bitrate };

static FILE *station_pipe(const char *command, const char *mode)
{
	FILE *fp;
	int i;
	assert(!strcmp(command, "/usr/sbin/wlanconfig ath-test list sta 2>/dev/null"));
	assert(!strcmp(mode, "r"));
	station_calls++;
	if (station_failure == 2) return NULL;
	fp = tmpfile();
	assert(fp);
	fputs("ADDR AID CHAN TXRATE RXRATE RSSI MINRSSI MAXRSSI IDLE\n", fp);
	fputs("02:00:00:00:00:ff 1 36 badM 12M -30\n", fp);
	/* A long diagnostic line must not be mistaken for a station in its next chunk. */
	for (i = 0; i < 511; i++) fputc('x', fp);
	fputs("02:00:00:00:00:ff 1 36 12M 12M -30\n", fp);
	for (i = 0; i < station_count; i++)
		fprintf(fp, "02:00:00:00:00:%02x 1 36 2882M 1441M -48 -60 -45 0\n", i % 256);
	rewind(fp);
	return fp;
}

static int station_close(FILE *fp)
{
	fclose(fp);
	return station_failure ? 256 : 0;
}

int main(void)
{
	int hw, ht, len, calls, i;
	char buf[IWINFO_BUFSIZE + 16];
	struct iwinfo_assoclist_entry entry;
	assert(sizeof(struct iwinfo_rate_entry) == 8);
	assert(offsetof(struct iwinfo_rate_entry, mhz) == 6 && offsetof(struct iwinfo_rate_entry, nss) == 7);
	assert(qca_name("wifi2") && qca_name("ath26"));
	assert(!qca_name("../etc/passwd") && !qca_name("ath2;id") && !qca_name(""));
	assert(!qca_name("abcdefghijklmnop"));
	assert(!qca_bitrate("ath-test", &hw) && hw == 2882000);
	nl_rate = 1200000;
	assert(!qca_bitrate("ath-test", &hw) && hw == 1200000);
	qca_parse_modes("11AXA_HE20 11AXA_HE160 11BEA_EHT20 11BEA_EHT320", &hw, &ht);
	assert((hw & (IWINFO_80211_AX | IWINFO_80211_BE)) == (IWINFO_80211_AX | IWINFO_80211_BE));
	assert(ht & IWINFO_HTMODE_EHT320);
	assert(!(ht & IWINFO_HTMODE_EHT160));
	assert(!(hw & IWINFO_80211_B));
	qca_parse_modes("11B 11G 11NG_HT40PLUS 11BEG_EHT40MINUS", &hw, &ht);
	assert(hw & IWINFO_80211_B);
	assert(ht == (IWINFO_HTMODE_HT40 | IWINFO_HTMODE_EHT40));
	assert(!qca_parse_station("02:00:00:00:00:01 1 36 2882M 1441M -48", &entry));
	assert(entry.mac[5] == 1 && entry.signal == -48 && entry.tx_rate.rate == 2882000);
	assert(entry.rx_rate.rate == 1441000 && entry.rx_rate.mcs == -1);
	assert(!qca_parse_station("02:00:00:00:00:01 1 36 2882M 1441M -48 padding IEEE80211_MODE_11AXA_HE80 2 2 0", &entry));
	assert(entry.rx_rate.is_he && entry.rx_rate.mhz == 80 && entry.rx_rate.nss == 2 && entry.rx_rate.mcs == -1);
	assert(!qca_parse_station("02:00:00:00:00:01 1 36 2882M 1441M -48 IEEE80211_MODE_11BEA_EHT320 2 2 0", &entry));
	assert(entry.rx_rate.is_eht && entry.rx_rate.is_320mhz && entry.rx_rate.mhz == 0);
	assert(!qca_parse_station("02:00:00:00:00:01 1 36 2882M 1441M -48 IEEE80211_MODE_11AXA_HE9999 99 99 0", &entry));
	assert(!entry.rx_rate.mhz && !entry.rx_rate.nss);
	assert(qca_parse_station("02:00:00:00:00:zz 1 36 1M 2M -50", &entry));
	assert(qca_parse_station("02:00:00:00:00:01 1 36 -1M 2M -50", &entry));
	assert(qca_parse_station("02:00:00:00:00:01 1 36 4294967296M 2M -50", &entry));
	assert(qca_parse_station("02:00:00:00:00:01 1 36 4294968M 2M -50", &entry));
	assert(qca_parse_station("02:00:00:00:00:01 1 36 1M 2M -129", &entry));
	assert(qca_parse_station("ADDR AID CHAN TXRATE RXRATE RSSI", &entry));
	assert(!qca_assoclist("ath-test", buf, &len) && len == 2 * sizeof(entry));
	memcpy(&entry, buf, sizeof(entry));
	assert(entry.signal == -48 && entry.noise == -96 && entry.rx_rate.rate == 1441000);
	individual_result = 0;
	assert(!qca_assoclist("ath-test", buf, &len));
	memcpy(&entry, buf, sizeof(entry));
	assert(entry.rx_packets == 11 && entry.signal == -45 && entry.rx_rate.rate == 2135200);
	individual_result = -1;
	nl_stations = 1;
	calls = station_calls;
	assert(!qca_assoclist("ath-test", buf, &len) && len == sizeof(entry));
	assert(station_calls == calls);
	memcpy(&entry, buf, sizeof(entry));
	assert(entry.signal == -60);
	nl_stations = 0;
	nl_result = -1;
	assert(!qca_assoclist("ath-test", buf, &len) && len == 2 * sizeof(entry));
	station_failure = 1;
	assert(qca_assoclist("ath-test", buf, &len) && len == 0);
	station_failure = 2;
	assert(qca_assoclist("ath-test", buf, &len) && len == 0);
	station_failure = 0;
	station_count = 0;
	assert(!qca_assoclist("ath-test", buf, &len) && len == 0);
	station_count = IWINFO_BUFSIZE / sizeof(entry) + 10;
	memset(buf, 0xa5, sizeof(buf));
	assert(!qca_assoclist("ath-test", buf, &len));
	assert(len == IWINFO_BUFSIZE / sizeof(entry) * sizeof(entry));
	for (i = IWINFO_BUFSIZE; i < sizeof(buf); i++) assert((unsigned char)buf[i] == 0xa5);
	calls = station_calls;
	assert(qca_assoclist("ath-test;id", buf, &len) && len == 0);
	assert(qca_assoclist("-bad", buf, &len) && len == 0);
	assert(station_calls == calls);
	return 0;
}
