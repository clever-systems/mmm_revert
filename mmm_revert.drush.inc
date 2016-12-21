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

  return $items;
}

function drush_mmm_revert() {
  $translations = _drush_mmm_revert_translation_features();

  _mmm_revert_invoke(dt('Update database'), 'updatedb', [], ['cache-clear' => FALSE])
  && _mmm_revert_invoke(dt('Enable master module'), 'pm-enable', ['master'])
  && _mmm_revert_invoke(dt('Master modules'), 'master-exec', [], ['no-cache-clear' => TRUE])
  && _mmm_revert_invoke(dt('Revert non-translation features'), 'fr-all', $translations)
  // Do not delete on language refresh to workaround bugs.
  && _mmm_revert_invoke(dt('Refresh strings'), 'language-refresh', [], [/* 'delete' => TRUE */])
  && (!$translations || _mmm_revert_invoke(dt('Revert translation features'), 'fr', $translations))
  && _mmm_revert_invoke(dt('Refresh translation info'), 'l10n-update-refresh')
  && _mmm_revert_invoke(dt('Update translations'), 'l10n-update', [], ['mode' => 'replace'])
  && _mmm_revert_invoke(dt('Clear caches'), 'cache-clear', ['all'])
  && _mmm_revert_invoke(dt('Refresh search API index'), 'search-api-index')
  && _mmm_revert_invoke(dt('Load entity cache'), 'ecload')
  ;
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
  if (!isset($commands[$cmd])) {
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
