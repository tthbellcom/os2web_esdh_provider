<?php

/**
 * @file
 * This file implements administration menus and functionality for
 * the provider module.
 */

/**
 * Page callback. Displays status page.
 */
function _os2web_esdh_provider_status() {
  ctools_include('plugins');
  $apis = array_keys(os2web_esdh_provider_ctools_plugin_type());
  $head = array('API name', 'Description', 'module', 'status');
  $rows = array();
  foreach ($apis as $api) {
    $plugins = ctools_get_plugins('os2web_esdh_provider', $api);
    if (count($plugins) > 0) {
      foreach ($plugins as $plugin) {
        $rows[] = array(
          $api,
          $plugin['description'],
          $plugin['module'],
          os2web_esdh_provider_has_api($api) ? 'Working' : 'Error',
        );
      }
    }
    else {
      $rows[] = array($api, 'Unavailable', 'Not enabled.', 'Error');
    }
  }

  // Show a table of watchdog entries if the debug flag is on.
  $watchdog_table = '';
  if (variable_get('os2web_esdh_provider_debug_info')) {
    $watchdog_messages = db_query('SELECT * FROM {watchdog} w WHERE w.type = :type AND w.severity = :severity ORDER BY w.timestamp DESC', array(':type' => 'acadre MM', ':severity' => WATCHDOG_WARNING));
    $watchdog_head = array(t('Warning messages in descending order'), t('Date'));
    foreach ($watchdog_messages as $row) {
      $watchdog_rows[] = array(
        t($row->message, unserialize($row->variables)),
        format_date($row->timestamp),
      );
    }
    $watchdog_table = theme('table', array('header' => $watchdog_head, 'rows' => $watchdog_rows));
  }

  // tth@bellcom : status for external meeting importer



  $status = '<h2>Meeting import: currently not running</h2>';

  if(!lock_acquire('os2web_esdh_provider_queue')){
    $status = '<h2>Meeting import: running!</h2><p>Processing:</p>';
  }


  $queue_full = variable_get('os2web_esdh_provider_queue');

  foreach($queue_full as $key => $queue){

    if(isset($queue['operations'])){
      $status .= drupal_render(drupal_get_form('os2web_esdh_provider_queue_form'));
      $status .= 'In queue:<br />';
      $queue_header = array(t('Publication id:'), t('uri'), t('Force'));

      foreach($queue['operations'] as $operation){
        $id = $operation[1][0]['id'];
        $uri = $operation[1][0]['uri'];
        $operation[1][1] ? $force = 'True' : $force = '';

        if(is_numeric($id)){
          $queue_rows[] = array($id, $uri, $force);
        }
      }
    }
  }

  $import_form = drupal_get_form('os2web_esdh_provider_import_form');
  return '<h2>' . t('ESDH Provider API plugin status') . '</h2>' .
      theme('table', array('header' => $head, 'rows' => $rows)) .
      '<h2>' . t('Import of MM Based ESDH documents. (Last import was run at %time)', array('%time' => format_date(variable_get('os2web_esdh_provider_last_import', 0)))) . '</h2>' .
      drupal_render($import_form) .
      $watchdog_table . $status . theme('table', array('header' => $queue_header, 'rows' => $queue_rows));
}

/**
 * Queue form
 */
function os2web_esdh_provider_queue_form($form_state){
  $form = array();
  $form['delete'] = array(
    '#type' => 'submit',
    '#value' => t('Delete queue'),
  );

  return $form;
}

/**
 * Submit handler for queue from
 */
function os2web_esdh_provider_queue_form_submit(&$form, &$form_state){
  // Empty queue
  variable_set('os2web_esdh_provider_queue', array());
  drupal_set_message(t('Deleted queue'));
}

/**
 * Import form.
 */
function os2web_esdh_provider_import_form() {
  $form = array();
  $form['run_import'] = array(
    '#type' => 'submit',
    '#value' => t('Run import'),
  );
  $form['group'] = array(
    '#type' => 'fieldset',
    '#title' => t('Extended options.'),
    '#collapsible' => TRUE,
    '#collapsed' => TRUE,
  );
  $form['group']['force'] = array(
    '#type' => 'checkbox',
    '#title' => t('Force recreation of nodes'),
    '#default_value' => FALSE,
    '#description' => t('WARNING: This will destroy existing nodes and replace with freshly imported. All node-id\s will change'),
  );
  $form['group']['focus'] = array(
    '#type' => 'textfield',
    '#title' => t('Focused import id'),
    '#description' => t('Ie. 1577 to reimport XML_1577.xml.'),
  );
  $form['group']['debug_info'] = array(
    '#type' => 'checkbox',
    '#title' => t('Output debug info on import.'),
    '#description' => t('Generates an output from watchdog, and print timer reads in log.'),
    '#default_value' => variable_get('os2web_esdh_provider_debug_info', FALSE),
  );
  $form['group']['debug_email'] = array(
    '#type' => 'textfield',
    '#title' => t('Email address to send status on import.'),
    '#description' => t('The email to send status mail to.'),
    '#default_value' => variable_get('os2web_esdh_provider_debug_email'),
  );
  return $form;
}

/**
 * Implements hook_FORM_submit().
 */
function os2web_esdh_provider_import_form_submit($form, &$form_state) {
  variable_set('os2web_esdh_provider_debug_info', $form_state['values']['debug_info']);
  variable_set('os2web_esdh_provider_debug_email', $form_state['values']['debug_email']);
  drupal_goto('admin/config/os2web/esdh_provider/importconfirm', array(
    'query' => array(
      'id' => $form_state['values']['focus'],
      'force' => $form_state['values']['force'],
    ),
  ));
  return;
}

/**
 * Implements hook_form().
 */
function _os2web_esdh_provider_status_confirm($form, &$form_state) {
  $id = $_GET['id'];
  $force = (int) $_GET['force'];
  $form['_meeting_id'] = array(
    '#type' => 'hidden',
    '#value' => $id,
  );
  $form['_meeting_force'] = array(
    '#type' => 'hidden',
    '#value' => $force,
  );
  $msg = '';
  if ($id > 0) {
    $msg = t('<p>Importing meeting with id !focus <br/>', array('!focus' => $id));
  }
  else {
    $msg = t('<p>Importing all meetings.<br/>');
  }
  if ($force) {
    $msg .= t('All imported nodes will be re-imported, detroying existing data and importing new.<br/>');
  }
  else {
    $msg .= t('Only new nodes will be imported, existing data is preserved.</p>');
  }
  $msg .= t('<p>Are you sure you want to run this import?</p>');
  return confirm_form($form, t('Import meetings'), isset($_GET['destination']) ? $_GET['destination'] : "admin/config/os2web/esdh_provider", $msg, t('Run'), t('Cancel'));
}

/**
 * Implements hook_FORM_submit().
 */
function _os2web_esdh_provider_status_confirm_submit($form, &$form_state) {
  if (!os2web_esdh_provider_has_api('mm')) {
    drupal_set_message(t('ERROR: No valid MM importer plugin active!'), 'error');
    return;
  }

  $external_queue = variable_get('os2web_esdh_provider_queue_enabled', FALSE);

  $force = $form_state['values']['_meeting_force'] == 1;
  $focus = array_map('trim', explode(',', $form_state['values']['_meeting_id']));
  if (count($focus) === 1 && $focus[0] === '') {
    $focus = array();
  }

  $batch = array(
    'title' => 'Dagsorden import',
    'operations' => array(),
    'file' => drupal_get_path('module', 'os2web_esdh_provider') . '/os2web_esdh_provider.mmapi.inc',
    'init_message' => t('Scanner filer'),
  );
  $meetings = os2web_esdh_provider_invoke('mm', 'get_import_list');
  foreach ($meetings as $meeting) {
    if (empty($focus) || in_array($meeting['id'], $focus)) {
      $batch['operations'][] = array(
        '_os2web_esdh_provider_import_meeting',
        array($meeting, $force));
    }
  }
  if (os2web_esdh_provider_supports('mm', 'post_import_process')) {
    $batch['operations'][] = array(
      'os2web_esdh_provider_invoke',
      array('mm', 'post_import_process'));
  }

  variable_set('os2web_esdh_provider_last_mm_import', $current_timestamp);

  if($external_queue){
    $queue_full = variable_get('os2web_esdh_provider_queue', array());
    $queue_full[time()] = $batch;

    variable_set('os2web_esdh_provider_queue', $queue_full );
    drupal_goto('admin/config/os2web/esdh_provider');
  }
  else {
    batch_set($batch);
    // This isn't entirely neccesary, but needed to make the redirect work.
    batch_process('admin/config/os2web/esdh_provider');
  }
}
