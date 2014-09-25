<?php
/**
 * @file
 * os2web_esdh_provider.queue.php
 *
 * Let the meetings importer run via CLI instead of HTTP
 *
 * Run with Drush php-script
 * "drush scr os2web_esdh_provider.queue.php"
 */

require_once 'os2web_esdh_provider.mmapi.inc';

if (lock_acquire('os2web_esdh_provider_queue', 10000)) {
  $queue_full = variable_get('os2web_esdh_provider_queue');

  if (!empty($queue_full)) {
    foreach ($queue_full as $key => $queue) {
      foreach ($queue['operations'] as $operation) {
        $operation[0]($operation[1][0], $operation[1][1]);
      }
      unset($queue_full[$key]);

      variable_set('os2web_esdh_provider_queue', $queue_full);
    }
  }
}

lock_release('os2web_esdh_provider_queue');
