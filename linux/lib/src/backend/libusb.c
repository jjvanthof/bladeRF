#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <errno.h>
#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <unistd.h>
#include <bladeRF.h>
#include <libusb-1.0/libusb.h>

#include "bladerf_priv.h"
#include "debug.h"

#define BLADERF_LIBUSB_TIMEOUT_MS 1000

#define EP_DIR_IN   LIBUSB_ENDPOINT_IN
#define EP_DIR_OUT  LIBUSB_ENDPOINT_OUT

#define EP_IN(x) (LIBUSB_ENDPOINT_IN | x)
#define EP_OUT(x) (x)

struct bladerf_lusb {
    libusb_device           *dev;
    libusb_device_handle    *handle;
    libusb_context          *context;
};

const struct bladerf_fn bladerf_lusb_fn;

static inline size_t min_sz(size_t a, size_t b)
{
    return a < b ? a : b;
}

static int vendor_command(struct bladerf *dev,
                          uint16_t cmd, uint8_t ep_dir, int *val)
{
    int buf;
    int status;
    struct bladerf_lusb *lusb = dev->backend;

    if( ep_dir == EP_DIR_IN ) {
        *val = 0 ;
    } else {
        buf = *val ;
    }
    status = libusb_control_transfer(
                lusb->handle,
                LIBUSB_RECIPIENT_INTERFACE | LIBUSB_REQUEST_TYPE_VENDOR | ep_dir,
                cmd,
                0,
                0,
                (unsigned char *)&buf,
                sizeof(buf),
                BLADERF_LIBUSB_TIMEOUT_MS
             );
    if (status < 0) {
        dbg_printf( "status < 0: %s\n", libusb_error_name(status) );
        if (status == LIBUSB_ERROR_TIMEOUT) {
            status = BLADERF_ERR_TIMEOUT;
        } else {
            status = BLADERF_ERR_IO;
        }
    } else if (status != sizeof(buf)) {
        dbg_printf( "status != sizeof(buf): %s\n", libusb_error_name(status) );
        status = BLADERF_ERR_IO;
    } else {
        if( ep_dir == EP_DIR_IN ) {
            *val = buf;
        }
        status = 0;
    }

    return status;
}

static int begin_fpga_programming(struct bladerf *dev)
{
    int result;
    int status = vendor_command(dev, BLADE_USB_CMD_BEGIN_PROG, EP_DIR_IN, &result);

    if (status < 0) {
        return status;
    } else {
        return 0;
    }
}

static int end_fpga_programming(struct bladerf *dev)
{
    int result;
    int status = vendor_command(dev, BLADE_USB_CMD_QUERY_FPGA_STATUS, EP_DIR_IN, &result);
    if (status < 0) {
        dbg_printf("Received response of (%d): %s\n",
                    status, libusb_error_name(status));
        return status;
    } else {
        return 0;
    }
}

int lusb_is_fpga_configured(struct bladerf *dev)
{
    int result;
    int status = vendor_command(dev, BLADE_USB_CMD_QUERY_FPGA_STATUS, EP_DIR_IN, &result);

    if (status < 0) {
        return status;
    } else {
        return result;
    }
}

int lusb_device_is_bladerf(libusb_device *dev)
{
    int err;
    int rv = 0;
    struct libusb_device_descriptor desc;

    err = libusb_get_device_descriptor(dev, &desc);
    if( err ) {
        dbg_printf( "Couldn't open libusb device - %s\n", libusb_error_name(err) );
    } else {
        if( desc.idVendor == USB_NUAND_VENDOR_ID && desc.idProduct == USB_NUAND_BLADERF_PRODUCT_ID ) {
            rv = 1;
        }
    }
    return rv;
}

int lusb_get_devinfo(libusb_device *dev, struct bladerf_devinfo *info)
{
    int status = 0;
    libusb_device_handle *handle;

    status = libusb_open( dev, &handle );
    if( status ) {
        dbg_printf( "Couldn't populate devinfo - %s\n", libusb_error_name(status) );
        status = BLADERF_ERR_IO;
    } else {
        /* Populate */
        info->backend = BACKEND_LIBUSB;
        info->serial = 0;
        info->usb_bus = libusb_get_bus_number(dev);
        info->usb_addr = libusb_get_device_address(dev);

        libusb_close( handle );
    }

    return status ;
}

static int lusb_open(struct bladerf **device, struct bladerf_devinfo *info)
{
    int status, i, n, inf;
    ssize_t count;
    struct bladerf *dev = NULL;
    struct bladerf_lusb *lusb = NULL;
    libusb_device **list;
    struct bladerf_devinfo thisinfo;

    libusb_context *context ;

    *device = NULL;

    /* Initialize libusb for device tree walking */
    status = libusb_init(&context);
    if( status ) {
        dbg_printf( "Could not initialize libusb: %s\n", libusb_error_name(status) );
        status = BLADERF_ERR_IO;
        goto lusb_open_done;
    }

    count = libusb_get_device_list( NULL, &list );
    /* Iterate through all the USB devices */
    for( i = 0, n = 0 ; i < count ; i++ ) {
        if( lusb_device_is_bladerf(list[i]) ) {
            /* Open the USB device and get some information */
            status = lusb_get_devinfo( list[i], &thisinfo );
            if( status ) {
                dbg_printf( "Could not open bladeRF device: %s\n", libusb_error_name(status) );
                status = BLADERF_ERR_IO;
                goto lusb_open__err_context;
            }
            thisinfo.instance = n++;

            /* Check to see if this matches the info stuct */
            if( bladerf_devinfo_matches( &thisinfo, info ) ) {
                /* Allocate backend structure and populate*/
                dev = (struct bladerf *)malloc(sizeof(struct bladerf));
                lusb = (struct bladerf_lusb *)malloc(sizeof(struct bladerf_lusb));

                /* Assign libusb function table, backend type and backend */
                dev->fn = &bladerf_lusb_fn;
                dev->backend = (void *)lusb;
                dev->backend_type = BACKEND_LIBUSB;

                /* Populate the backend information */
                lusb->context = context;
                lusb->dev = list[i];
                lusb->handle = NULL;
                status = libusb_open(list[i], &lusb->handle);
                if( status ) {
                    dbg_printf( "Could not open bladeRF device: %s\n", libusb_error_name(status) );
                    status = BLADERF_ERR_IO;
                    goto lusb_open__err_device_list;
                }

                /* Claim interfaces */
                for( inf = 0 ; inf < 3 ; inf++ ) {
                    status = libusb_claim_interface(lusb->handle, inf);
                    if( status ) {
                        dbg_printf( "Could not claim interface %i - %s\n", inf, libusb_error_name(status) );
                        status = BLADERF_ERR_IO;
                        goto lusb_open__err_device_list;
                    }
                }

                dbg_printf( "Claimed all inferfaces successfully\n" );
                break ;
            }
        }
    }

/* XXX I'd prefer if we made a call here to lusb_close(), but that would result
 *     in an attempt to release interfaces we haven't claimed... thoughts? */
lusb_open__err_device_list:
    libusb_free_device_list( list, 1 );
    if (status != 0) {
        if (lusb->handle) {
            libusb_close(lusb->handle);
        }

        free(lusb);
        free(dev);
        lusb = NULL;
        dev = NULL;
    }

lusb_open__err_context:
    if( dev == NULL ) {
        libusb_exit(context);
    }

lusb_open_done:
    if (!status) {

        if (dev) {
            *device = dev;
        } else {
            dbg_printf("No devices available on the libusb backend.\n");
            status = BLADERF_ERR_NODEV;
        }
    }

    return status;
}

static int lusb_close(struct bladerf *dev)
{
    int status = 0;
    int inf = 0;
    struct bladerf_lusb *lusb = dev->backend;


    for( inf = 0 ; inf < 2 ; inf++ ) {
        status = libusb_release_interface(lusb->handle, inf);
        if (status) {
            dbg_printf("error releasing interface %i\n", inf);
            status = BLADERF_ERR_IO;
        }
    }

    libusb_close(lusb->handle);
    libusb_exit(lusb->context);
    free(dev->backend);
    free(dev);

    return status;
}

static int lusb_load_fpga(struct bladerf *dev, uint8_t *image, size_t image_size)
{
    unsigned int wait_count;
    int status = 0, val;
    int transferred = 0;
    struct bladerf_lusb *lusb = dev->backend;

    /* Make sure we are using the configuration interface */
    status = libusb_set_interface_alt_setting(lusb->handle, USB_IF_CONFIG, 0);
    if(status) {
        bladerf_set_error(&dev->error, ETYPE_BACKEND, status);
        dbg_printf( "alt_setting issue: %s\n", libusb_error_name(status) );
        return BLADERF_ERR_IO;;
    }

    /* Begin programming */
    status = begin_fpga_programming(dev);
    if (status < 0) {
        bladerf_set_error(&dev->error, ETYPE_BACKEND, status);
        return BLADERF_ERR_IO;
    }

    /* Send the file down */
    status = libusb_bulk_transfer(lusb->handle, 0x2, image, image_size,
                                  &transferred, 5 * BLADERF_LIBUSB_TIMEOUT_MS);
    if (status < 0) {
        bladerf_set_error(&dev->error, ETYPE_BACKEND, status);
        return BLADERF_ERR_IO;
    }

    /*  End programming */
    status = end_fpga_programming(dev);
    if (status) {
        bladerf_set_error(&dev->error, ETYPE_BACKEND, status);
        return BLADERF_ERR_IO;
    }

    /* Poll FPGA status to determine if programming was a success */
    wait_count = 10;
    status = 0;

    while (wait_count > 0 && status == 0) {
        status = lusb_is_fpga_configured(dev);
        if (status == 1) {
            break;
        }

        wait_count--;
        usleep(10000);
    }

    /* Failed to determine if FPGA is loaded */
    if (status < 0) {
        bladerf_set_error(&dev->error, ETYPE_BACKEND, status);
        return  BLADERF_ERR_IO;
    } else if (wait_count == 0) {
        bladerf_set_error(&dev->error, ETYPE_LIBBLADERF, BLADERF_ERR_TIMEOUT);
        return BLADERF_ERR_TIMEOUT;
    }

    /* Go into RF link mode by selecting interface 1 */
    status = libusb_set_interface_alt_setting(lusb->handle, 1, 0);
    if(status) {
        dbg_printf("libusb_set_interface_alt_setting: %s", libusb_error_name(status));
    }

    val = 1;
    status = vendor_command(dev, BLADE_USB_CMD_RF_RX, EP_DIR_OUT, &val);
    if(status) {
        dbg_printf("Could not enable RF RX (%d): %s\n", status, libusb_error_name(status) );
    }

    status = vendor_command(dev, BLADE_USB_CMD_RF_TX, EP_DIR_OUT, &val);
    if(status) {
        dbg_printf("Could not enable RF TX (%d): %s\n", status, libusb_error_name(status) );
    }

    return status;
}

/* Note: n_bytes is rounded up to a multiple of the sector size here */
static int erase_flash(struct bladerf *dev, int sector_offset, int n_bytes)
{
    int status = 0;
    off_t i;
    int sector_to_erase;
    int erase_ret;
    const int n_sectors = FLASH_BYTES_TO_SECTORS(n_bytes);
    struct bladerf_lusb *lusb = dev->backend;

    assert(sector_offset < FLASH_NUM_SECTORS);
    assert((sector_offset + n_sectors) < FLASH_NUM_SECTORS);

    dbg_printf("Erasing %d sectors starting @ sector %d\n",
                n_sectors, sector_offset);

    for (i = sector_offset; i < (sector_offset + n_sectors) && !status; i++) {
        sector_to_erase = sector_offset + i;
        status = libusb_control_transfer(
                                lusb->handle,
                                LIBUSB_RECIPIENT_INTERFACE |
                                    LIBUSB_REQUEST_TYPE_VENDOR |
                                    EP_DIR_IN,
                                BLADE_USB_CMD_FLASH_ERASE,
                                0,
                                i,
                                (unsigned char *)&erase_ret,
                                sizeof(erase_ret),
                                BLADERF_LIBUSB_TIMEOUT_MS);

        if (status != sizeof(erase_ret) || !erase_ret) {
            dbg_printf("Failed to erase sector %d\n", sector_to_erase);
            if (status < 0) {
                dbg_printf("libusb status: %s\n", libusb_error_name(status));
            } else if (!erase_ret) {
                dbg_printf("Received erase failure status from FX3.\n");
            } else {
                dbg_printf("Unexpected read size: %d\n", status);
            }
            status = BLADERF_ERR_IO;
        } else {
            dbg_printf("Erased sector %d...\n", sector_to_erase);
            status = 0;
        }
    }

    return status;
}

static int verify_flash(struct bladerf *dev, int page_offset,
                        uint8_t *image, size_t n_bytes)
{
    int status = 0;
    int page_i, check_i, n_read;
    int read_size = dev->speed ? FLASH_PAGE_SIZE: 64;
    int pages_to_read = FLASH_BYTES_TO_PAGES(n_bytes);
    struct bladerf_lusb *lusb = dev->backend;

    uint8_t page_buf[FLASH_PAGE_SIZE];
    uint8_t *image_page;

    dbg_printf("Verifying with read size = %d\n", read_size);

    assert(page_offset < FLASH_NUM_PAGES);
    assert((page_offset + n_bytes) < FLASH_NUM_PAGES);

    for (page_i = page_offset;
         page_i < (page_offset + pages_to_read) && !status;
         page_i++) {

        /* Read back a page */
        n_read = 0;
        do {
            status = libusb_control_transfer(
                        lusb->handle,
                        LIBUSB_RECIPIENT_INTERFACE |
                            LIBUSB_REQUEST_TYPE_VENDOR |
                            EP_DIR_IN,
                        BLADE_USB_CMD_FLASH_READ,
                        0,
                        page_i,
                        page_buf + n_read,
                        read_size,
                        BLADERF_LIBUSB_TIMEOUT_MS);


            if (status != read_size) {
                if (status < 0) {
                    dbg_printf("Failed to read back page %d: %s\n", page_i,
                               libusb_error_name(status));
                } else {
                    dbg_printf("Unexpected read size: %d\n", status);
                }

                status = BLADERF_ERR_IO;
            } else {
                n_read += read_size;
                status = 0;
            }
        } while (n_read < FLASH_PAGE_SIZE && !status);

        /* Verify the page */
        for (check_i = 0; check_i < FLASH_PAGE_SIZE && !status; check_i++) {
            image_page = image + (page_i - page_offset) * FLASH_PAGE_SIZE + check_i;
            if (page_buf[check_i] != *image_page) {
                fprintf(stderr,
                        "Error: bladeRF firmware verification failed at byte %d"
                        " Read 0x%02X, expected 0x%02X\n",
                        page_i * FLASH_PAGE_SIZE + check_i,
                        page_buf[check_i],
                        *image_page);

                status = BLADERF_ERR_IO;
            }
        }
    }

    return status;
}

static int write_flash(struct bladerf *dev, int page_offset,
                        uint8_t *data, size_t data_size)
{
    int status = 0;
    int i;
    int n_write;
    int write_size = dev->speed ? FLASH_PAGE_SIZE : 64;
    int pages_to_write = FLASH_BYTES_TO_PAGES(data_size);
    struct bladerf_lusb *lusb = dev->backend;
    uint8_t *data_page;

    dbg_printf("Flashing with write size = %d\n", write_size);

    assert(page_offset < FLASH_NUM_PAGES);
    assert((page_offset + pages_to_write) < FLASH_NUM_PAGES);

    for (i = page_offset; i < (page_offset + pages_to_write) && !status; i++) {
        n_write = 0;
        do {
            data_page = data + (i - page_offset) * FLASH_PAGE_SIZE + n_write;
            status = libusb_control_transfer(
                        lusb->handle,
                        LIBUSB_RECIPIENT_INTERFACE |
                            LIBUSB_REQUEST_TYPE_VENDOR |
                            EP_DIR_OUT,
                        BLADE_USB_CMD_FLASH_WRITE,
                        0,
                        i,
                        (unsigned char *)data_page,
                        write_size,
                        BLADERF_LIBUSB_TIMEOUT_MS);

            if (status != write_size) {
                if (status < 0) {
                    dbg_printf("Failed to write page %d: %s\n", i,
                            libusb_error_name(status));
                } else {
                    dbg_printf("Got unexpected write size: %d\n", status);
                }
                status = BLADERF_ERR_IO;
            } else {
                n_write += write_size;
                status = 0;
            }
        } while (n_write < FLASH_PAGE_SIZE && !status);
    }

    return status;
}

static int lusb_flash_firmware(struct bladerf *dev,
                               uint8_t *image, size_t image_size)
{
    int status;
    struct bladerf_lusb *lusb = dev->backend;

    status = libusb_set_interface_alt_setting(lusb->handle, USB_IF_SPI_FLASH, 0);
    if (status) {
        dbg_printf("Failed to set interface: %s\n", libusb_error_name(status));
        status = BLADERF_ERR_IO;
    }

    if (status == 0) {
        status = erase_flash(dev, 0, image_size);
    }

    if (status == 0) {
        status = write_flash(dev, 0, image, image_size);
    }

    if (status == 0) {
        status = verify_flash(dev, 0, image, image_size);
    }

    /* A reset will be required at this point, so there's no sense in
     * bothering to set the interface back to USB_IF_RF_LINK */

    return status;
}

static int lusb_get_serial(struct bladerf *dev, uint64_t *serial)
{
    return 0;
}

static int lusb_get_fw_version(struct bladerf *dev,
                               unsigned int *maj, unsigned int *min)
{
    return 0;
}

static int lusb_get_fpga_version(struct bladerf *dev,
                                 unsigned int *maj, unsigned int *min)
{
    dbg_printf("FPGA currently does not have a version number.\n");
    *maj = 0;
    *min = 0;
    return 0;
}

static int lusb_get_device_speed(struct bladerf *dev, int *device_speed)
{
    int speed;
    int status = 0;
    struct bladerf_lusb *lusb = dev->backend;

    speed = libusb_get_device_speed(lusb->dev);
    if (speed == LIBUSB_SPEED_SUPER) {
        *device_speed = 1;
    } else if (speed == LIBUSB_SPEED_HIGH) {
        *device_speed = 0;
    } else {
        /* FIXME - We should have a better error code...
         * BLADERF_ERR_UNSUPPORTED? */
        dbg_printf("Got unsupported or unknown device speed: %d\n", speed);
        status = BLADERF_ERR_INVAL;
    }

    return status;
}

/* Returns BLADERF_ERR_* on failure */
static int access_peripheral(struct bladerf_lusb *lusb, int per, int dir,
                                struct uart_cmd *cmd)
{
    uint8_t buf[16] = { 0 };    /* Zeroing out to avoid some valgrind noise
                                 * on the reserved items that aren't currently
                                 * used (i.e., bytes 4-15 */

    int status, libusb_status, transferred;

    /* Populate the buffer for transfer */
    buf[0] = UART_PKT_MAGIC;
    buf[1] = dir | per | 0x01;
    buf[2] = cmd->addr;
    buf[3] = cmd->data;

    /* Write down the command */
    libusb_status = libusb_bulk_transfer(lusb->handle, 0x02, buf, 16,
                                           &transferred,
                                           BLADERF_LIBUSB_TIMEOUT_MS);

    if (libusb_status < 0) {
        dbg_printf("could not access peripheral\n");
        return BLADERF_ERR_IO;
    }

    /* If it's a read, we'll want to read back the result */
    transferred = 0;
    libusb_status = status =  0;
    while (libusb_status == 0 && transferred != 16) {
        libusb_status = libusb_bulk_transfer(lusb->handle, 0x82, buf, 16,
                                             &transferred,
                                             BLADERF_LIBUSB_TIMEOUT_MS);
    }

    if (libusb_status < 0) {
        return BLADERF_ERR_IO;
    }

    /* Save off the result if it was a read */
    if (dir == UART_PKT_MODE_DIR_READ) {
        cmd->data = buf[3];
    }

    return status;
}

static int lusb_gpio_write(struct bladerf *dev, uint32_t val)
{
    int i = 0;
    int status = 0;
    struct uart_cmd cmd;
    struct bladerf_lusb *lusb = dev->backend;

    for (i = 0; status == 0 && i < 4; i++) {
        cmd.addr = i;
        cmd.data = (val>>(8*i))&0xff;
        status = access_peripheral(
                                    lusb,
                                    UART_PKT_DEV_GPIO,
                                    UART_PKT_MODE_DIR_WRITE,
                                    &cmd
                                  );

        if (status < 0) {
            break;
        }
    }

    if (status < 0) {
        bladerf_set_error(&dev->error, ETYPE_LIBBLADERF, status);
    }

    return status;
}

static int lusb_gpio_read(struct bladerf *dev, uint32_t *val)
{
    int i = 0;
    int status = 0;
    struct uart_cmd cmd;
    struct bladerf_lusb *lusb = dev->backend;

    *val = 0;
    for(i = 0; status == 0 && i < 4; i++) {
        cmd.addr = i;
        cmd.data = 0xff;
        status = access_peripheral(
                                    lusb,
                                    UART_PKT_DEV_GPIO, UART_PKT_MODE_DIR_READ,
                                    &cmd
                                  );

        if (status < 0) {
            break;
        }

        *val |= (cmd.data << (8*i));
    }

    if (status < 0) {
        bladerf_set_error(&dev->error, ETYPE_LIBBLADERF, status);
    }

    return status;
}

static int lusb_si5338_write(struct bladerf *dev, uint8_t addr, uint8_t data)
{
    int status;
    struct uart_cmd cmd;
    struct bladerf_lusb *lusb = dev->backend;

    cmd.addr = addr;
    cmd.data = data;

    status = access_peripheral(lusb, UART_PKT_DEV_SI5338,
                               UART_PKT_MODE_DIR_WRITE, &cmd);

    if (status < 0) {
        bladerf_set_error(&dev->error, ETYPE_LIBBLADERF, status);
    }

    return status;
}

static int lusb_si5338_read(struct bladerf *dev, uint8_t addr, uint8_t *data)
{
    int status = 0;
    struct uart_cmd cmd;
    struct bladerf_lusb *lusb = dev->backend;

    cmd.addr = addr;
    cmd.data = 0xff;

    status = access_peripheral(lusb, UART_PKT_DEV_SI5338,
                               UART_PKT_MODE_DIR_READ, &cmd);

    if (status < 0) {
        bladerf_set_error(&dev->error, ETYPE_LIBBLADERF, status);
    } else {
        *data = cmd.data;
    }

    return status;
}

static int lusb_lms_write(struct bladerf *dev, uint8_t addr, uint8_t data)
{
    int status;
    struct uart_cmd cmd;
    struct bladerf_lusb *lusb = dev->backend;

    cmd.addr = addr;
    cmd.data = data;

    status = access_peripheral(lusb, UART_PKT_DEV_LMS,
                                UART_PKT_MODE_DIR_WRITE, &cmd);

    if (status < 0) {
        bladerf_set_error(&dev->error, ETYPE_LIBBLADERF, status);
    }

    return status;
}

static int lusb_lms_read(struct bladerf *dev, uint8_t addr, uint8_t *data)
{
    int status;
    struct uart_cmd cmd;
    struct bladerf_lusb *lusb = dev->backend;

    cmd.addr = addr;
    cmd.data = 0xff;
    status = access_peripheral(lusb, UART_PKT_DEV_LMS,
                               UART_PKT_MODE_DIR_READ, &cmd);

    if (status < 0) {
        bladerf_set_error(&dev->error, ETYPE_LIBBLADERF, status);
    } else {
        *data = cmd.data;
    }

    return status;
}

static int lusb_dac_write(struct bladerf *dev, uint16_t value)
{
    int status;
    struct uart_cmd cmd;
    struct bladerf_lusb *lusb = dev->backend;

    cmd.word = value;
    status = access_peripheral(lusb, UART_PKT_DEV_VCTCXO,
                               UART_PKT_MODE_DIR_WRITE, &cmd);

    if (status < 0) {
        bladerf_set_error(&dev->error, ETYPE_LIBBLADERF, status);
    }

    return status;
}

static ssize_t lusb_tx(struct bladerf *dev, bladerf_format_t format, void *samples,
                       size_t n, struct bladerf_metadata *metadata)
{
    size_t bytes_total, bytes_remaining;
    struct bladerf_lusb *lusb = (struct bladerf_lusb *)dev->backend;
    uint8_t *samples8 = (uint8_t *)samples;
    int transferred, status;

    assert(format==FORMAT_SC16);

    bytes_total = bytes_remaining = c16_samples_to_bytes(n);

    while( bytes_remaining > 0 ) {
        transferred = 0;
        status = libusb_bulk_transfer(
                    lusb->handle,
                    EP_OUT(0x1),
                    samples8,
                    bytes_remaining,
                    &transferred,
                    BLADERF_LIBUSB_TIMEOUT_MS
                );
        if( status < 0 ) {
            dbg_printf( "Error transmitting samples (%d): %s\n", status, libusb_error_name(status) );
            return BLADERF_ERR_IO;
        } else {
            assert(transferred > 0);
            bytes_remaining -= transferred;
            samples8 += transferred;
        }
    }

    return bytes_to_c16_samples(bytes_total - bytes_remaining);
}

static ssize_t lusb_rx(struct bladerf *dev, bladerf_format_t format, void *samples,
                       size_t n, struct bladerf_metadata *metadata)
{
    ssize_t bytes_total, bytes_remaining = c16_samples_to_bytes(n) ;
    struct bladerf_lusb *lusb = (struct bladerf_lusb *)dev->backend;
    uint8_t *samples8 = (uint8_t *)samples;
    int transferred, status;

    assert(format==FORMAT_SC16);

    bytes_total = bytes_remaining = c16_samples_to_bytes(n);

    while( bytes_remaining ) {
        transferred = 0;
        status = libusb_bulk_transfer(
                    lusb->handle,
                    EP_IN(0x1),
                    samples8,
                    bytes_remaining,
                    &transferred,
                    BLADERF_LIBUSB_TIMEOUT_MS
                );
        if( status < 0 ) {
            dbg_printf( "Error reading samples (%d): %s\n", status, libusb_error_name(status) );
            return BLADERF_ERR_IO;
        } else {
            assert(transferred > 0);
            bytes_remaining -= transferred;
            samples8 += transferred;
        }
    }

    return bytes_to_c16_samples(bytes_total - bytes_remaining);
}

int lusb_probe(struct bladerf_devinfo_list *info_list)
{
    int status, i, n;
    ssize_t count;
    libusb_device **list;
    struct bladerf_devinfo info;

    libusb_context *context ;

    /* Initialize libusb for device tree walking */
    status = libusb_init(&context);
    if( status ) {
        dbg_printf( "Could not initialize libusb: %s\n", libusb_error_name(status) );
        goto lusb_probe_done;
    }

    count = libusb_get_device_list( NULL, &list );
    /* Iterate through all the USB devices */
    for( i = 0, n = 0 ; i < count && status == 0 ; i++ ) {
        if( lusb_device_is_bladerf(list[i]) ) {
            /* Open the USB device and get some information */
            status = lusb_get_devinfo( list[i], &info );
            if( status ) {
                dbg_printf( "Could not open bladeRF device: %s\n", libusb_error_name(status) );
            } else {
                info.instance = n++;
                status = bladerf_devinfo_list_add(info_list, &info);
                if( status ) {
                    dbg_printf( "Could not add device to list: %s\n", bladerf_strerror(status) );
                }
            }
        }
    }
    libusb_free_device_list(list,1);
    libusb_exit(context);
lusb_probe_done:
    return status;
}

const struct bladerf_fn bladerf_lusb_fn = {
    .probe              = lusb_probe,

    .open               = lusb_open,
    .close              = lusb_close,

    .load_fpga          = lusb_load_fpga,
    .is_fpga_configured = lusb_is_fpga_configured,

    .flash_firmware     = lusb_flash_firmware,

    .get_serial         = lusb_get_serial,
    .get_fw_version     = lusb_get_fw_version,
    .get_fpga_version   = lusb_get_fpga_version,
    .get_device_speed   = lusb_get_device_speed,

    .gpio_write         = lusb_gpio_write,
    .gpio_read          = lusb_gpio_read,

    .si5338_write       = lusb_si5338_write,
    .si5338_read        = lusb_si5338_read,

    .lms_write          = lusb_lms_write,
    .lms_read           = lusb_lms_read,

    .dac_write          = lusb_dac_write,

    .rx                 = lusb_rx,
    .tx                 = lusb_tx,
};

