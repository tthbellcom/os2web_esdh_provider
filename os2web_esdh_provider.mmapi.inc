<?php
/**
 * @file
 * This file implements the importing functions of the MM functionality and depends on a
 * plugin to do the actual filereads
 */

function hr_size($size) {
  $unit = array('b', 'kb', 'mb', 'gb', 'tb', 'pb');
  return @round($size / pow(1024, ($i = floor(log($size, 1024)))), 2) . ' ' . $unit[$i];
}

function err_debug($msg = FALSE) {
  $mem = hr_size(memory_get_usage());
  $timer = number_format(timer_read('mm_import') / 1000, 2) . "sec.";
//  error_log("Import debug $msg $mem $timer");
}

/**
 * Imports meeting metadata into a node.
 *
 * @param string $meeting_id
 *   Meeting id
 *
 * @return int
 *   Node id
 */
function _os2web_esdh_provider_import_meeting($meeting_id, $force = FALSE, &$context = NULL) {
  if (!lock_acquire(__FUNCTION__ . $meeting_id['id'])) {
    return;
  }
  timer_start('mm_import');
  err_debug('Init.');
  $meetings = os2web_esdh_provider_invoke('mm', 'import_meeting', $meeting_id);
  err_debug('Meetings loaded.');
  $nids = array();
  foreach ($meetings as $meeting) {

    // Run check to see if meeting is already imported.
    $query = new EntityFieldQuery();
    $result = $query->entityCondition('entity_type', 'node')
        ->propertyCondition('type', 'meeting')
        ->fieldCondition('field_os2web_meetings_system_id', 'value', $meeting['system_id'], '=')
        ->execute();
    if (isset($result['node'])) {
      $nid = array_shift($result['node'])->nid;
      if (!$meeting['publish']) {
        // Meeting needs to be unpublished.
        error_log('MARK - ' . basename(__FILE__) . ':' . __LINE__ . ' in ' . __FUNCTION__ . ' - Unpublish nid: ' . $nid);
        node_delete($nid);
        watchdog('ESDH', 'Unpublished %nid .', array('%nid' => $nid), WATCHDOG_INFO);
        continue;
      }
      if ($force) {
        error_log('MARK - ' . basename(__FILE__) . ':' . __LINE__ . ' in ' . __FUNCTION__ . ' - Republish nid: ' . $nid);
        node_delete($nid);
        watchdog('ESDH', 'Removed %nid prior to import.', array('%nid' => $nid), WATCHDOG_DEBUG);
        // We create a new node object.
        $node = new stdClass();
      }
      else {
        error_log('MARK - ' . basename(__FILE__) . ':' . __LINE__ . ' in ' . __FUNCTION__ . ' - Skipping nid: ' . $nid);
        // watchdog('ESDH', 'Meeting already imported in %nid. Skiping.', array('%nid' => $nid), WATCHDOG_DEBUG);
        continue;
      }
    }
    else {
      if (!$meeting['publish']) {
        continue;
      }
      // We create a new node object.
      $node = new stdClass();
    }

    // Or any other content type you want.
    $node->type = "os2web_meetings_meeting";
    // Or any language code if Locale module is enabled. More on this below.
    $node->language = LANGUAGE_NONE;
    // Or any id you wish.
    $node->uid = 1;
    // Set some default values.
    node_object_prepare($node);
    // Set field values:
    $node->title = $meeting['title'];
    $node->field_os2web_meetings_abbr[LANGUAGE_NONE][0]['value'] = substr($meeting['description'], 0, 49);
    // If field has a format, you need to define it. Here we define a default filtered_html format for a body field.
    $node->field_os2web_meetings_abbr[LANGUAGE_NONE][0]['format'] = 'filtered_html';
    // The date.
    $node->field_os2web_meetings_date[LANGUAGE_NONE][0]['value'] = $meeting['meeting_date'];
    $node->field_os2web_meetings_date[LANGUAGE_NONE][0]['value2'] = $meeting['meeting_date'];
    $node->field_os2web_meetings_date[LANGUAGE_NONE][0]['timezone'] = 'Europe/Berlin';
    $node->field_os2web_meetings_date[LANGUAGE_NONE][0]['date_type'] = 'datetime';
    // Proper id tagging.
    $node->field_os2web_meetings_id[LANGUAGE_NONE][0]['value'] = $meeting['meeting_id'];
    $node->field_os2web_meetings_system_id[LANGUAGE_NONE][0]['value'] = $meeting['system_id'];
    // Type import.
    $node->field_os2web_meetings_type[LANGUAGE_NONE][0]['value'] = $meeting['type'];

    // Make sure the committee term is created and get the term.
    $node->field_os2web_meetings_committee[LANGUAGE_NONE][0]['tid'] = _acadre_esdh_import_term($meeting['committee'], 'os2web_meetings_tax_committee')->tid;
    if(!empty($meeting['location'])) {
      $node->field_os2web_meetings_location[LANGUAGE_NONE][0]['tid'] = _acadre_esdh_import_term($meeting['location'], 'os2web_meetings_tax_location')->tid;
    }
    // Add QBrick info @FIXME This should be in a node_save hook in qbrick module.
    if (function_exists('_os2web_qbrick_get_movie_id')) {
      $movie_id = _os2web_qbrick_get_movie_id($node);
      if ($movie_id != FALSE) {
        $meeting['movie_id'] = $movie_id;
        $chapters = _os2web_qbrick_get_chapters($movie_id);
        $node->field_os2web_meetings_qbrickref[LANGUAGE_NONE][0]['id'] = $movie_id;
        $node->field_os2web_meetings_qbrickref[LANGUAGE_NONE][0]['seek'] = 0;
        foreach ($meeting['items'] as &$item) {
          $item['chapter'] = isset($chapters[$item['id']]) ? $chapters[$item['id']] : 0;
          $item['movie_id'] = $movie_id;
        }
      }
      else {
        $node->field_os2web_meetings_qbrickref = array();
      }
    }
    // Add full document.
    if (FALSE !== ($file = _os2web_esdh_provider_import_file($meeting['full_doc']))) {
      $node->field_os2web_meetings_full_doc[LANGUAGE_NONE][] = (array) $file;
    }
    // Import items.
    if (is_array($meeting['items'])) {
      $item_nids = _os2web_esdh_provider_import_items($meeting['items']);
      foreach ($item_nids as $nid) {
        $node->field_os2web_meetings_bullets[LANGUAGE_NONE][]['target_id'] = $nid;
      }
    }

    // Save the meeting node.
    node_submit($node);
    $node->created = $meeting['creation_date'];
    node_save($node);
    error_log("MARK - Created: \$node->nid = " . print_r($node->nid, 1));
    watchdog('ESDH', 'Imported agenda id: %aid (Meeting: %mid) into node %nid .', array(
      '%aid' => $meeting['system_id'],
      '%mid' => $meeting['meeting_id'],
      '%nid' => $node->nid), WATCHDOG_DEBUG);
    $nids[] = $node->nid;
    err_debug('Meeting saved.');
  }
  variable_set('os2web_esdh_provider_last_import', time());
  lock_release(__FUNCTION__ . $meeting_id['id']);
  return $nids;
}

/**
 * Imports item metadata into nodes.
 * @access protected
 *
 * @param array $items
 *   List of items to import.
 *
 * @return array
 *   List of imported node ids
 */
function _os2web_esdh_provider_import_items($items) {
  $nids = array();
  foreach ($items as $item) {
    $node = new stdClass();
    $node->type = "os2web_meetings_bullet";
    $node->language = LANGUAGE_NONE;
    $node->uid = 1;
    $node->title = $item['title'];
//    $node->body['und'][0]['value'] = $item['body'];
    // Set some default values.
    node_object_prepare($node);
    if ($item['access'] !== 'SKAL PUBLICERES') {
      $node->field_os2web_meetings_bul_closed[LANGUAGE_NONE][]['value'] = 0;
      node_submit($node);
      node_save($node);
      $nids[] = $node->nid;
    }
    else {
      $node->field_os2web_meetings_bul_closed[LANGUAGE_NONE][]['value'] = 1;
      if (isset($item['movie_id']) && is_numeric($item["chapter"])) {
        $node->field_os2web_meetings_qbrickref[LANGUAGE_NONE][0]['id'] = $item['movie_id'];
        $node->field_os2web_meetings_qbrickref[LANGUAGE_NONE][0]['seek'] = $item["chapter"];
      }
      // Gather the bullets for the item.
      if (isset($item['bullets']) && is_array($item['bullets'])) {
        $bullet_nids = _os2web_esdh_provider_import_bullets($item['bullets']);
        foreach ($bullet_nids as $nid) {
          $node->field_os2web_meetings_attach[LANGUAGE_NONE][]['target_id'] = $nid;
        }
      }
      else {
        $node->field_os2web_meetings_attach[LANGUAGE_NONE] = array();
      }
      // Gather enclosures.
      foreach ($item['enclosures'] as $enclosure) {
        if ($enclosure['access']) {
          if ($file = _os2web_esdh_provider_import_file($enclosure)) {
            $node->field_os2web_meetings_enclosures[LANGUAGE_NONE][] = (array) $file;
          }
          else {
            watchdog('ESDH', 'There was an error importing file %uri ', array('%uri' => $enclosure['uri']), WATCHDOG_ERROR);
          }
        }
      }
      node_submit($node);
      node_save($node);
      $nids[] = $node->nid;
      err_debug('Item saved');
    }
  }
  return $nids;
}

/**
 * Imports bullet metadata into nodes.
 * @access protected
 *
 * @param array $bullets
 *   List of bullets
 *
 * @return array
 *   List of imported node ids
 */
function _os2web_esdh_provider_import_bullets($bullets) {
  // Since we need to run this often, we reset the execution timer to avoid timeouts.
  set_time_limit(180);
  $nids = array();
  foreach ($bullets as $idx => $bullet) {
    $node = new stdClass();
    $node->type = "os2web_meetings_bullet_attach";
    $node->language = LANGUAGE_NONE;
    $node->uid = 1;
    $node->title = $bullet['title'];
    // Set some default values.
    node_object_prepare($node);
    // Let's add body field.
    $node->field_os2web_meetings_bpa_body[$node->language][0]['value'] = trim($bullet['body']);
    // If field has a format, you need to define it. Here we define a default filtered_html format for a body field.
    $node->field_os2web_meetings_bpa_body[$node->language][0]['format'] = 'filtered_html';
    node_submit($node);
    node_save($node);
    $nids[$idx] = $node->nid;
    err_debug('Bullet saved.');
  }
  return $nids;
}

/**
 * Imports a file metadata into a managed drupal file.
 * @access protected
 *
 * @param array $file_meta
 *   File metadata
 *
 * @return object
 *   Drupal standard file object
 */
function _os2web_esdh_provider_import_file($file_meta) {
  // Since we need to run this often, we reset the execution timer to avoid timeouts.
  set_time_limit(300);
  $uri_in = $file_meta['uri'];

  $pinfo = pathinfo($uri_in);
  if ($file_meta['title'] !== '') {
    $fname = $file_meta['title'];
  }
  else {
    $fname = $pinfo['filename'];
  }

  $uri_out = $pinfo['dirname'] . '/' . _os2web_esdh_provider_sanetize_filename($fname . '.' . strtolower($pinfo['extension']));
  $uri_pdf = $pinfo['dirname'] . '/' . _os2web_esdh_provider_sanetize_filename($fname . '.pdf');

  $uri = FALSE;
  if ($uri_tmp = _os2web_esdh_provider_similar_file_exists(drupal_realpath($uri_pdf))) {
    // We have an already converted pdf file.
    $uri = $uri_pdf;
    $convert = FALSE;
  }
  elseif ($uri_tmp = _os2web_esdh_provider_similar_file_exists(drupal_realpath($uri_out))) {
//    $uri_out = implode('/', array(
//      variable_get('os2web_meetings_path', ACADRE_MM_IMPORT_DIR),
//      $folder,
//      basename($uri_tmp)));
    // File already copied.
    $convert = TRUE;
    $uri = $uri_out;
  }
  else {
    // Completely unprocessed file.
    $uri_old = $uri_in;
    $uri_in = _os2web_esdh_provider_similar_file_exists(drupal_realpath($uri_in));
    if ($uri_in === FALSE || FALSE === file_unmanaged_copy($uri_in, $uri_out, FILE_EXISTS_REPLACE)) {
      // Bummed out on copy.
      watchdog('acadre MM', 'File copy failed for %infile -> %outfile. Check folder permissions.', array('%infile' => $uri_old, '%outfile' => $uri_out), WATCHDOG_WARNING);
      return FALSE;
    }
    else {
      $convert = TRUE;
      $uri = $uri_out;
    }
  }

  if (FALSE === $uri) {
    watchdog('acadre MM', 'Processing of %infile failed. If exists, check permissions.', array('%infile', $uri_in), WATCHDOG_WARNING);
    return FALSE;
  }

  if (file_uri_scheme($uri) != 'public') {
    watchdog('acadre MM', 'Invalid scheme for uri %uri, Possibly absolut path ?', array('%uri', $uri), WATCHDOG_ERROR);
    error_log("Var: \$uri = " . print_r($uri, 1));
    error_log("Var: \$uri_in = " . print_r($uri_in, 1));
    error_log("Var: \$uri_out = " . print_r($uri_out, 1));
    error_log("Var: \$uri_pdf = " . print_r($uri_pdf, 1));
  }

  $fid = db_select('file_managed', 'fm')
          ->fields('fm', array('fid'))
          ->condition('uri', $uri)
          ->execute()->fetchField();

  $file = new stdClass();
  if ($fid) {
    $file->fid = $fid;
  }
  $file->uri = $uri;
  $file->display = 1;
  $file->field_titel[LANGUAGE_NONE][]['value'] = $file_meta['title'];
  $file->filename = drupal_basename($file->uri);
  $file->filemime = file_get_mimetype($file->uri);
  $file->uid = 1;
  $file->status = FILE_STATUS_PERMANENT;
  $file = file_save($file);

  // Schedule for PDF conversion
  // $convert = FALSE; // DEBUG.
  if ($convert) {
    drupal_load('module', 'os2web_adlib_api');
    os2web_adlib_api_convert_to_pdf($file->fid, $file->filename);
    watchdog('acadre MM', 'File %filename (fid: %fid) queued for PDF conversion.', array('%filename' => $file->filename, '%fid' => $file->fid), WATCHDOG_INFO);
  }

  err_debug('File imported.');
  // watchdog('acadre MM', 'Imported file %filename (fid: %fid).', array('%filename' => $file->filename, '%fid' => $file->fid), WATCHDOG_DEBUG);
  return $file;
}

/**
 * Imports a termname into a vacabulary. Does dupe check.
 *
 * @param string $term_name
 *   Term machine name
 * @param string $vocab
 *   Vocabulary machine name
 *
 * @return object
 *   Drupal standard Term object
 */
function _acadre_esdh_import_term($term_name, $vocab) {
  // TODO: Process members of the committee.
  // FIXME: MAke vocab name configurable.

  $vid = db_select('taxonomy_vocabulary', 'tv')
      ->fields('tv', array('vid'))
      ->condition('machine_name', $vocab)
      ->execute()
      ->fetchField();

  $term = new stdClass();
  $term->vid = $vid;
  $term->name = $term_name;

  $tid = db_select('taxonomy_term_data', 'td')
          ->fields('td', array('tid'))
          ->condition('name', $term->name)
          ->condition('vid', $term->vid)
          ->execute()->fetchObject();

  if (isset($tid->tid) && $tid->tid > 0) {
    $term->tid = (int) $tid->tid;
  }
  taxonomy_term_save($term);

  return $term;
}

/**
 * Page callback. Displays a technical overview of an imported meeting.
 */
function _os2web_esdh_provider_node_tab_page($nid, $op = FALSE) {
  $html = "";
  $node = node_load($nid);
  $meeting_id = $node->field_os2web_meetings_id[LANGUAGE_NONE][0]['value'];
  $system_id = $node->field_os2web_meetings_system_id[LANGUAGE_NONE][0]['value'];
  $filename = 'Unknown';
  $files = file_scan_directory(variable_get('os2web_meetings_path', ACADRE_MM_IMPORT_DIR), '/\.xml$/i', array('recurse' => FALSE));
  foreach ($files as $file) {
    $xml = simplexml_load_file(drupal_realpath($file->uri));
    if (is_object($xml)) {
      $xml_mid = (string) array_shift($xml->xpath("//table[@name='meeting']/fields/field/@sysid"));
      if ($meeting_id == $xml_mid) {
        $filename = $file->filename;
        $agenda = $xml->xpath("//table[@name='producedAgenda']/fields/field[@sysid='$system_id']/../field");
        if (!empty($agenda)) {
          break;
        }
      }
    }
  }

  if ($op) {
    switch ($op) {
      case 'reimport':
        break;

      default:
        drupal_set_message($op . ' not yet implemented.', 'error');
        break;
    }
    drupal_goto('node/' . $nid . '/control');
  }

  $html .= "<h1>" . $node->title . "</h1>";

  $head = array('Node id', 'Meeting id', 'System id', 'Filename');
  $rows[] = array(
    $node->nid,
    $meeting_id,
    $system_id,
    $filename,
  );
  $html .= theme('table', array('header' => $head, 'rows' => $rows));
  $head = array('Attribute', 'Value');
  $rows = array();
  foreach ($agenda as $attr) {
    foreach ($attr->attributes() as $key => $val) {
      $rows[] = array($key, $val);
    }
  }
  $html .= theme('table', array('header' => $head, 'rows' => $rows));
  return $html;
}

/**
 * Sanetizes filename, removing illegal characters.
 *
 * @param string $filename
 *   Filename
 *
 * @return string
 *   Sanetized filename with same extension.
 */
function _os2web_esdh_provider_sanetize_filename($filename) {
  // Replace whitespace.
  // $filename = str_replace(' ', '_', $filename);
  // Remove remaining unsafe characters.
  // $filename = preg_replace('![^0-9A-Za-z_.- ]!', '', $filename);
  $filename = preg_replace('![/"]!', '-', $filename);
  // Remove multiple consecutive non-alphabetical characters.
  $filename = preg_replace('/(_)_+|(\.)\.+|(-)-+/', '\\1\\2\\3', $filename);
  // Force lowercase to prevent issues on case-insensitive file systems.
  // $filename = strtolower($filename);
  $pi = pathinfo($filename);
  if (isset($pi['extension'])) {
    return substr(trim($pi['filename']), 0, 160) . '.' . $pi['extension'];
  }
  else {
    return substr(trim($pi['filename']), 0, 160);
  }
}

/**
 * Case insensitive fileexists(). Code from comments on php.net.
 *
 * @param string $filename
 *   Filename
 *
 * @return mixed
 *   False if file not found, case corrected filename if found.
 */
function _os2web_esdh_provider_similar_file_exists($filename) {
  if (file_exists($filename)) {
    // error_log(basename(__FILE__) . ':' . __LINE__ . ' _os2web_meetings_similar_file_exists FILE EXISTS = ' . $filename);
    return $filename;
  }

  $dir = dirname($filename);
  $files = glob($dir . '/*');
  $lcase_filename = strtolower($filename);
  foreach ($files as $file) {
    if (strtolower($file) == $lcase_filename) {
      return $file;
    }
  }
  return FALSE;
}

/**
 * Page callback for the import service call.
 */
function _os2web_esdh_provider_import($id = FALSE) {
  if (!os2web_esdh_provider_has_api('mm')) {
    drupal_json_output(array('status' => 'error', 'message' => 'Not available.'));
  }
  $meetings = os2web_esdh_provider_invoke('mm', 'get_import_list');
  $meeting_id = FALSE;
  foreach ($meetings as $meeting) {
    if ($meeting['id'] == $id) {
      $meeting_id = $meeting;
      break;
    }
  }
  if ($meeting_id === FALSE) {
    _os2web_esdh_provider_import_output();
  }
  else {
    $nids = _os2web_esdh_provider_import_meeting($meeting_id, TRUE);
    if (os2web_esdh_provider_supports('mm', 'post_import_process')) {
      os2web_esdh_provider_invoke('mm', 'post_import_process');
    }
    if (count($nids) > 0) {
      _os2web_esdh_provider_import_output($id, $nids);
    }
    else {
      _os2web_esdh_provider_import_output($id);
    }
  }
}

/**
 * Helper function to dump output from REST importer.
 */
function _os2web_esdh_provider_import_output($id, $nids) {
  if (isset($id)) {
    $msg = 'Meeting not found.';
  }
  if (isset($nids) && empty($nids)) {
    $msg = 'Meeting ' . $id . ' imported, but no nodes created.';
  }
  else {
    $urls = array();
    foreach ($nids as $nid) {
      $urls[] = url('node/' . $nid, array('absolute' => TRUE));
    }
    $msg = 'Meeting ' . $id . ' imported to nodes: ' . implode(',', $urls);
  }
  drupal_add_http_header('Content-Type', 'application/json');
  echo $msg;
}
