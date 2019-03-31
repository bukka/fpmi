fpmi: $(SAPI_FPMI_PATH)

$(SAPI_FPMI_PATH): $(PHP_GLOBAL_OBJS) $(PHP_BINARY_OBJS) $(PHP_FASTCGI_OBJS) $(PHP_FPMI_OBJS)
	$(BUILD_FPMI)

install-fpmi: $(SAPI_FPMI_PATH)
	@echo "Installing PHP FPMi binary:        $(INSTALL_ROOT)$(sbindir)/"
	@$(mkinstalldirs) $(INSTALL_ROOT)$(sbindir)
	@$(mkinstalldirs) $(INSTALL_ROOT)$(localstatedir)/log
	@$(mkinstalldirs) $(INSTALL_ROOT)$(localstatedir)/run
	@$(INSTALL) -m 0755 $(SAPI_FPMI_PATH) $(INSTALL_ROOT)$(sbindir)/$(program_prefix)php-fpmi$(program_suffix)$(EXEEXT)

	@if test -f "$(INSTALL_ROOT)$(sysconfdir)/php-fpmi.conf"; then \
		echo "Installing PHP FPMi defconfig:     skipping"; \
	else \
		echo "Installing PHP FPMi defconfig:     $(INSTALL_ROOT)$(sysconfdir)/" && \
		$(mkinstalldirs) $(INSTALL_ROOT)$(sysconfdir)/php-fpmi.d; \
		$(INSTALL_DATA) sapi/fpmi/php-fpmi.conf $(INSTALL_ROOT)$(sysconfdir)/php-fpmi.conf.default; \
		$(INSTALL_DATA) sapi/fpmi/www.conf $(INSTALL_ROOT)$(sysconfdir)/php-fpmi.d/www.conf.default; \
	fi

	@echo "Installing PHP FPMi man page:      $(INSTALL_ROOT)$(mandir)/man8/"
	@$(mkinstalldirs) $(INSTALL_ROOT)$(mandir)/man8
	@$(INSTALL_DATA) sapi/fpmi/php-fpmi.8 $(INSTALL_ROOT)$(mandir)/man8/php-fpmi$(program_suffix).8

	@echo "Installing PHP FPMi status page:   $(INSTALL_ROOT)$(datadir)/fpmi/"
	@$(mkinstalldirs) $(INSTALL_ROOT)$(datadir)/fpmi
	@$(INSTALL_DATA) sapi/fpmi/status.html $(INSTALL_ROOT)$(datadir)/fpmi/status.html
