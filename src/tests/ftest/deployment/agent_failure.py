#!/usr/bin/python
"""
  (C) Copyright 2022 Intel Corporation.

  SPDX-License-Identifier: BSD-2-Clause-Patent
"""
import time
from datetime import datetime
import os
import threading

from ior_test_base import IorTestBase
from ior_utils import IorCommand
from general_utils import report_errors, get_journalctl
from command_utils_base import CommandFailure
from job_manager_utils import get_job_manager


class AgentFailure(IorTestBase):
    # pylint: disable=too-many-ancestors
    """Test class Description: Verify agent failure is properly handled.

    :avocado: recursive
    """
    def run_ior_collect_error(self, results, job_num, file_name, clients):
        """Run IOR command and store error in results.

        Args:
            results (dict): A dictionary object to store the ior metrics
            job_num (int): Assigned job number
            file_name (str): File name used for self.ior_cmd.test_file.
            client (list): Client hostnames to run IOR from.
        """
        ior_cmd = IorCommand()
        ior_cmd.get_params(self)
        ior_cmd.set_daos_params(
            group=self.server_group, pool=self.pool, cont_uuid=self.container.uuid)
        testfile = os.path.join("/", file_name)
        ior_cmd.test_file.update(testfile)

        manager = get_job_manager(
            test=self, class_name="Mpirun", job=ior_cmd, subprocess=self.subprocess,
            mpi_type="mpich")
        manager.assign_hosts(clients, self.workdir, self.hostfile_clients_slots)
        ppn = self.params.get("ppn", '/run/ior/client_processes/*')
        manager.ppn.update(ppn, 'mpirun.ppn')
        manager.processes.update(None, 'mpirun.np')

        try:
            ior_output = manager.run()
            results[job_num] = [True]
            # For debugging.
            results[job_num].extend(IorCommand.get_ior_metrics(ior_output))
            # We'll verify the error message.
            results[job_num].append(ior_output.stderr_text)
        except CommandFailure as error:
            results[job_num] = [False, "IOR failed: {}".format(error)]

    def test_agent_failure(self):
        """Jira ID: DAOS-9385.

        1. Create a pool and a container.
        2. Run IOR.
        3. Stop daos_agent process while IOR is running.
        4. Check the error on the client side. When daos_agent is killed in the middle of
        IOR, the file write completes successfully, but the pool disconnect at the end
        fails, so we get the error message that includes -1005. This step is more like a
        verification of the test itself rather than the product.
        5. Verify journalctl shows the log that the agent is stopped. Call:
        journalctl --system -t daos_agent --since <before> --until <after>
        This step verifies that DAOS, or daos_agent process in this case, prints useful
        logs for the user to troubleshoot the issue, which in this case the application
        can’t be used.
        6. Restart daos_agent.
        7. Run IOR again. It should succeed this time without any error. This step
        verifies that DAOS can recover from the fault with minimal human intervention.

        :avocado: tags=all,full_regression
        :avocado: tags=hw,large
        :avocado: tags=deployment,fault_management,agent_failure
        :avocado: tags=agent_failure_basic
        """
        # 1. Create a pool and a container.
        self.add_pool()
        self.add_container(self.pool)

        # 2. Run IOR.
        ior_results = {}
        job_num = 1
        self.log.info("Run IOR with thread")
        job = threading.Thread(
            target=self.run_ior_collect_error,
            args=[ior_results, job_num, "test_file_1", self.hostlist_clients])

        self.log.info("Start IOR 1 (thread)")
        job.start()

        # We need to stop daos_agent while IOR is running, so need to wait for a few
        # seconds for IOR to start.
        self.log.info("Sleep 5 sec")
        time.sleep(5)

        errors = []

        # 3. Stop daos_agent process while IOR is running.
        since = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        self.log.info("Stopping agent")
        stop_agent_errors = self.stop_agents()
        for error in stop_agent_errors:
            self.log.debug(error)
            errors.append(error)
        until = datetime.now().strftime('%Y-%m-%d %H:%M:%S')

        # Wait until the IOR thread ends.
        job.join()

        # 4. Verify the error from the IOR command.
        self.log.info("--- IOR results 1 ---")
        self.log.info(ior_results)
        ior_error = ior_results[job_num][-1]
        if "-1005" not in ior_error:
            errors.append("-1005 is not in IOR error! {}".format(ior_error))

        # 5. Verify journalctl shows the log that the agent is stopped.
        results = get_journalctl(
            hosts=self.hostlist_clients, since=since, until=until,
            journalctl_type="daos_agent")
        self.log.info("journalctl results = %s", results)
        if "shutting down" not in results[0]["data"]:
            msg = "Agent shut down message not found in journalctl! Output = {}".format(
                results)
            errors.append(msg)

        # 6. Restart agent.
        self.log.info("Restart agent")
        self.start_agent_managers()

        # 7. Run IOR again.
        self.log.info("Start IOR 2")
        self.run_ior_collect_error(
            job_num=job_num, results=ior_results, file_name="test_file_2",
            clients=self.hostlist_clients)

        # Verify that there's no error this time.
        self.log.info("--- IOR results 2 ---")
        self.log.info(ior_results)
        ior_error = ior_results[job_num][-1]
        if ior_error:
            errors.append("Error found in second IOR run! {}".format(ior_error))

        self.log.info("########## Errors ##########")
        report_errors(test=self, errors=errors)
        self.log.info("############################")
