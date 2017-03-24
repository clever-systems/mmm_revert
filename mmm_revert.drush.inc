<?php
/**
 * @file mmm_revert.drush.inc
 */
use Drush\Log\LogLevel;

/**
 * Implements hook_drush_command().
 */
function mmm_revert_drush_command() {
  $items = array();

  $items['mmm-revert'] = array(
    'description' => 'Reverts a site.',
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_FULL,
  );
  $items['mmm-crontab-revert'] = array(
    'description' => 'Revert crontab from snippets in directory.',
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_CONFIGURATION,
    'options' => [
      'directories' => 'Directories to find snippets. Defaults to "../crontab.d".'
    ],
  );
  $items['mmm-crontab-set'] = array(
    'description' => 'Update crontab entries for this drupal root.',
    'bootstrap' => DRUSH_BOOTSTRAP_DRUPAL_ROOT,
    'arguments' => [
      'file file ...' => 'Files to get crontab entries from.',
    ],
  );

  return $items;
}

function drush_mmm_revert() {
  if (_drush_mmm_revert_is_d8()) {
    // D8
    _mmm_revert_invoke(dt('Update database'), 'updatedb', [], ['cache-clear' => FALSE])
    && _mmm_revert_invoke(dt('Import config'), 'config-import')
    && _mmm_revert_invoke(dt('Revert crontab'), 'mmm-crontab-revert')
    ;
  }
  else {
    // D7
    $translations = _drush_mmm_revert_translation_features();
    // Optionally use patch https://www.drupal.org/node/2502911
    $l10n_update_options = isset(drush_get_commands()['l10n-update']['options']['use-remote'])
      ? ['use-remote' => 0] : [];

    _mmm_revert_invoke(dt('Update database'), 'updatedb', [], ['cache-clear' => FALSE])
    && (module_exists('master') || _mmm_revert_invoke(dt('Enable master module'), 'pm-enable', ['master']))
    && _mmm_revert_invoke(dt('Master modules'), 'master-exec', [], ['no-cache-clear' => TRUE])
    && _mmm_revert_invoke(dt('Revert non-translation features'), 'fr-all', $translations)
    // Do not delete on language refresh to workaround bugs.
    && _mmm_revert_invoke(dt('Refresh strings'), 'language-refresh', [], [/* 'delete' => TRUE */])
    && (!$translations || _mmm_revert_invoke(dt('Revert translation features'), 'fr', $translations))
    // @todo Consider optional drush sqlq "TRUNCATE TABLE l10n_update_file; TRUNCATE TABLE  l10n_update_project;"
    && _mmm_revert_invoke(dt('Refresh translation info'), 'l10n-update-refresh', [], $l10n_update_options)
    && _mmm_revert_invoke(dt('Update translations'), 'l10n-update', [], ['mode' => 'replace'] + $l10n_update_options)
    && _mmm_revert_invoke(dt('Clear caches'), 'cache-clear', ['all'])
    && _mmm_revert_invoke(dt('Refresh search API index'), 'search-api-index')
    && _mmm_revert_invoke(dt('Revert crontab'), 'mmm-crontab-revert')
    && _mmm_revert_invoke(dt('Load entity cache'), 'ecload')
    ;
  }
}

function _drush_mmm_revert_translation_features() {
  $result = drush_invoke_process('@self', 'features-list', [], ['quiet' => TRUE]);
  if (isset($result['object'])) {
    $features = array_keys($result['object']);
    $translations = preg_grep('/_translations$/', $features);
    return $translations;
  }
  return [];
}

function _mmm_revert_invoke($msg, $cmd, $args = [], $options = []) {
  $t_args = ['@msg' => $msg];
  drush_log(dt('', $t_args), LogLevel::OK);

  $commands = drush_get_commands();
  if (!isset($commands[$cmd]) || !_mmm_revert_check_drupal_dependencies($commands[$cmd])) {
    drush_log(dt('Skip: @msg', $t_args), LogLevel::OK);
    return TRUE;
  }

  drush_log(dt('Start: @msg', $t_args), LogLevel::OK);
  $result = drush_invoke_process('@self', $cmd, $args, ['y' => TRUE] + $options);
  $success = $result || drush_get_option('simulate');
  if ($success) {
    drush_log(dt('Done: @msg', $t_args), LogLevel::SUCCESS);
  }
  else {
    drush_log(dt('Aborting: @msg', $t_args), LogLevel::ERROR);
  }
  return $success;
}

/**
 * Check drupal dependencies.
 *
 * @param array $command
 * @return boolean
 *
 * Like @see drush_enforce_requirement_drupal_dependencies().
 */
function _mmm_revert_check_drupal_dependencies($command) {
  if (!empty($command['drupal dependencies'])) {
    drush_include_engine('drupal', 'environment');
    foreach ($command['drupal dependencies'] as $dependency) {
      if(!drush_module_exists($dependency)) {
        return FALSE;
      }
    }
  }
  return TRUE;
}

function drush_mmm_revert_mmm_crontab_revert() {
  // Get $environment_name
  if (_drush_mmm_revert_is_d8()) {
    global $settings;
    $environment_name = isset($settings['mmm']['environment'])
      ? $settings['mmm']['environment'] : 'NONE';
  }
  else {
    global $conf;
    $environment_name = isset($conf['mmm']['environment'])
      ? $conf['mmm']['environment'] : 'NONE';
  }

  // Get $directories
  $directories = drush_get_option('directories', '../crontab.d/ENV:../crontab.d/common;../crontab.d');
  if (!is_array($directories)) {
    $directories = explode(':', $directories);
  }
  $directories = preg_replace('#\.ENV\.#u', $environment_name, $directories);

  // Sort all file paths by their basename to merge files from all dirs.
  $file_basenames_by_path = [];
  foreach ($directories as $directory) {
    $found_paths = (array) glob($directory . '/*');
    foreach ($found_paths as $found_path) {
      if (is_file($found_path)) {
        $file_basenames_by_path[$found_path] = basename($found_path);
      }
    }
  }
  asort($file_basenames_by_path);
  $paths = array_keys($file_basenames_by_path);
  return drush_invoke_process('@self', 'mmm-crontab-set', $paths);
}

// @todo unset.
function drush_mmm_revert_mmm_crontab_set() {
  $prefix = 'DRUPAL_ROOT=' . DRUPAL_ROOT;
  $files = func_get_args();

  $crontab_additions = $prefix . "\n";
  // Use drush magick to look up full alias including site url.
  // @see drush_sitealias_site_set().
  $crontab_additions .= 'cd $DRUPAL_ROOT; drush site-set @self' . "\n";
  foreach ($files as $file) {
    $file_content = file_get_contents($file);
    if ($file_content === FALSE) {
      return drush_set_error('drush_mmm_crontab:file_invalid', dt('File invalid: !file', ['!file' => $file]));
    }
    $crontab_additions .= $file_content . "\n";
  }
  $crontab_additions .= '#END DRUPAL_ROOT';
  $crontab_additions = _mmm_revert_remove_empty_lines($crontab_additions) . "\n";

  $success = drush_shell_exec('crontab -l');
  $output = implode("\n", drush_shell_exec_output());
  if (!$success) {
    if (preg_match('#^no crontab for #u', drush_shell_exec_output())) {
      $output = '';
    }
    else {
      return drush_set_error('drush_mmm_crontab:crontab_invalid', dt('Could not retrieve crontab, outputr: !o', ['!o' => $output]));
    }
  }

  $old_crontab = _mmm_revert_remove_empty_lines($output);

  drush_log(dt("Old crontab:\n!crontab", ['!crontab' => $old_crontab]));
  drush_log(dt("Adding to crontab:\n!crontab", ['!crontab' => $crontab_additions]));

  // Remove old cron lines.
  $prefix_quoted = preg_quote($prefix, '#');
  $new_crontab = preg_replace("~(^|\n)$prefix_quoted\n.*?\n#END DRUPAL_ROOT\n~us", "\n", $old_crontab);
  // Add new cron lines.
  $new_crontab .= $crontab_additions;
  drush_log(dt("New crontab:\n!crontab", ['!crontab' => $new_crontab]));

  // Work around drush over-sanitizing newlines.
  /** @see _drush_escapeshellarg_linux */
  $new_crontab_encoded = str_replace("\n", '\n', $new_crontab);
  $new_crontab_escaped = escapeshellarg($new_crontab_encoded);
  $success = drush_shell_exec("printf $new_crontab_escaped |crontab -");
  $output = implode("\n", drush_shell_exec_output());
  if (!$success) {
    return drush_set_error('drush_mmm_crontab:crontab_set', dt('Could not set crontab: !o', ['!o' => $output]));
  }
}

/**
 * Filter crontab partials.
 *
 * @param string $string
 * @return string
 */
function _mmm_revert_remove_empty_lines($string) {
  $lines = preg_split('#\r?\n#', $string);
  $filtered = array_filter($lines);
  $result = implode("\n", $filtered) . "\n";
  return $result;
}

/**
 * @return bool
 */
function _drush_mmm_revert_is_d8() {
  return file_exists(DRUPAL_ROOT . '/core/lib/Drupal.php');
}
