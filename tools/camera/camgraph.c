/* camgraph: dump media controller graph (entities, pads, links) */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/media.h>
int main(int argc, char **argv)
{
	const char *dev = argc > 1 ? argv[1] : "/dev/media0";
	int fd = open(dev, O_RDWR);
	if (fd < 0) { perror(dev); return 1; }
	struct media_device_info info;
	if (ioctl(fd, MEDIA_IOC_DEVICE_INFO, &info) == 0)
		printf("driver=%s model=%s\n", info.driver, info.model);
	struct media_entity_desc e;
	for (unsigned int i = 0;; i++) {
		memset(&e, 0, sizeof(e));
		e.id = i | MEDIA_ENT_ID_FLAG_NEXT;
		if (ioctl(fd, MEDIA_IOC_ENUM_ENTITIES, &e) < 0)
			break;
		printf("entity %u: %s type=%08x flags=%08x pads=%u links=%u\n",
		       e.id, e.name, e.type, e.flags, e.pads, e.links);
	}
	/* enumerate links for each entity */
	for (unsigned int i = 0;; i++) {
		struct media_links_enum le;
		struct media_pad_desc *pads;
		struct media_link_desc *links;
		memset(&e, 0, sizeof(e));
		e.id = i | MEDIA_ENT_ID_FLAG_NEXT;
		if (ioctl(fd, MEDIA_IOC_ENUM_ENTITIES, &e) < 0)
			break;
		pads = calloc(e.pads, sizeof(*pads));
		links = calloc(e.links, sizeof(*links));
		if (!pads || !links) return 1;
		memset(&le, 0, sizeof(le));
		le.entity = e.id;
		le.pads = pads;
		le.links = links;
		if (ioctl(fd, MEDIA_IOC_ENUM_LINKS, &le) == 0) {
			for (unsigned int l = 0; l < e.links; l++)
				printf("  %s link[%u]: src_ent=%u src_idx=%u flags=%08x dst_ent=%u dst_idx=%u\n",
				       e.name, l, links[l].source.entity, links[l].source.index,
				       links[l].flags, links[l].sink.entity, links[l].sink.index);
		}
		free(pads); free(links);
	}
	return 0;
}
