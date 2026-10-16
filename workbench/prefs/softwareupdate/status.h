/* Copyright (C) 2026 John KNIPPER <code@jkn.me>.
 * Distributed under the AROS Public License, version 1.1. */
#ifndef SOFTWARE_UPDATE_STATUS_H
#define SOFTWARE_UPDATE_STATUS_H
#define UPDATE_STATUS_SIZE 192
struct UpdateStatus {
    char installed[UPDATE_STATUS_SIZE],previous[UPDATE_STATUS_SIZE];
    char pending[UPDATE_STATUS_SIZE],delivered[UPDATE_STATUS_SIZE];
};
void SoftwareUpdateReadStatus(struct UpdateStatus *status);
#endif
